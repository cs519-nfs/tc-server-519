/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) CohortFS LLC, 2015
 * Author: Daniel Gryniewicz dang@cohortfs.com
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

/* attrs.c
 * CoWFS attribute caching handle object
 */

#include "config.h"

#include "avltree.h"
#include "fsal.h"
#include "fsal_convert.h"
#include "FSAL/access_check.h"
#include "../cowfs_methods.h"
#include "attrs.h"
#include "nfs4_acls.h"

struct cowfs_acl_entry {
	struct gsh_buffdesc	fa_key;		/**< Key for tree */
	struct avltree_node	fa_node;	/**< AVL tree node */
	fsal_acl_data_t		fa_acl;		/**< Actual ACLs */
};

static struct avltree cowfs_acl_tree = {0};

/**
 * @brief CoWFS acl comparator for AVL tree walk
 *
 */
static int cowfs_acl_cmpf(const struct avltree_node *lhs,
			const struct avltree_node *rhs)
{
	struct cowfs_acl_entry *lk, *rk;

	lk = avltree_container_of(lhs, struct cowfs_acl_entry, fa_node);
	rk = avltree_container_of(rhs, struct cowfs_acl_entry, fa_node);
	if (lk->fa_key.len != rk->fa_key.len)
		return (lk->fa_key.len < rk->fa_key.len) ? -1 : 1;

	return memcmp(lk->fa_key.addr, rk->fa_key.addr, lk->fa_key.len);
}

static struct cowfs_acl_entry *cowfs_acl_lookup(struct gsh_buffdesc *key)
{
	struct cowfs_acl_entry key_entry;
	struct avltree_node *node;

	key_entry.fa_key = *key;
	node = avltree_lookup(&key_entry.fa_node, &cowfs_acl_tree);
	if (!node)
		return NULL;

	return avltree_container_of(node, struct cowfs_acl_entry, fa_node);
}

static struct cowfs_acl_entry *cowfs_acl_locate(struct fsal_obj_handle *obj)
{
	struct cowfs_acl_entry *fa_entry;
	struct avltree_node *node;
	struct gsh_buffdesc key;

	obj->obj_ops.handle_to_key(obj, &key);

	fa_entry = cowfs_acl_lookup(&key);
	if (fa_entry) {
		LogDebug(COMPONENT_FSAL, "found");
		return fa_entry;
	}

	LogDebug(COMPONENT_FSAL, "create");
	fa_entry = gsh_calloc(1, sizeof(struct cowfs_acl_entry));

	fa_entry->fa_key = key;
	node = avltree_insert(&fa_entry->fa_node, &cowfs_acl_tree);
	if (unlikely(node)) {
		/* Race won */
		gsh_free(fa_entry);
		fa_entry = avltree_container_of(node, struct cowfs_acl_entry,
						fa_node);
	} else {
		fa_entry->fa_acl.aces = (fsal_ace_t *) nfs4_ace_alloc(0);
	}

	return fa_entry;
}

void cowfs_acl_init(void)
{
	if (cowfs_acl_tree.cmp_fn == NULL)
		avltree_init(&cowfs_acl_tree, cowfs_acl_cmpf, 0);
}

void cowfs_acl_release(struct gsh_buffdesc *key)
{
	struct cowfs_acl_entry *fa_entry;

	fa_entry = cowfs_acl_lookup(key);
	if (!fa_entry)
		return;

	avltree_remove(&fa_entry->fa_node, &cowfs_acl_tree);
	gsh_free(fa_entry);
}

fsal_status_t cowfs_sub_getattrs(struct cowfs_fsal_obj_handle *cowfs_hdl,
			       int fd, attrmask_t request_mask,
			       struct attrlist *attrib)
{
	fsal_acl_status_t status;
	struct cowfs_acl_entry *fa;
	fsal_acl_data_t acldata;
	fsal_acl_t *acl;

	LogDebug(COMPONENT_FSAL, "Enter");

	if (attrib->acl != NULL) {
		/* We should never be passed attributes that have an
		 * ACL attached, but just in case some future code
		 * path changes that assumption, let's not release the
		 * old ACL properly.
		 */
		int acl_status;

		acl_status = nfs4_acl_release_entry(attrib->acl);

		if (acl_status != NFS_V4_ACL_SUCCESS)
			LogCrit(COMPONENT_FSAL,
				"Failed to release old acl, status=%d",
				acl_status);

		attrib->acl = NULL;
	}

	fa = cowfs_acl_locate(&cowfs_hdl->obj_handle);
	if (!fa->fa_acl.naces) {
		/* No ACLs yet */
		FSAL_UNSET_MASK(attrib->mask, ATTR_ACL);

		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	}

	fsal_print_acl(COMPONENT_FSAL, NIV_FULL_DEBUG,
		       (fsal_acl_t *)&fa->fa_acl);
	acldata.naces = fa->fa_acl.naces;
	acldata.aces = (fsal_ace_t *) nfs4_ace_alloc(acldata.naces);
	memcpy(acldata.aces, fa->fa_acl.aces,
	       acldata.naces * sizeof(fsal_ace_t));

	acl = nfs4_acl_new_entry(&acldata, &status);
	if (!acl)
		return fsalstat(ERR_FSAL_FAULT, status);
	fsal_print_acl(COMPONENT_FSAL, NIV_FULL_DEBUG, acl);
	attrib->acl = acl;
	FSAL_SET_MASK(attrib->mask, ATTR_ACL);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t cowfs_sub_setattrs(struct cowfs_fsal_obj_handle *cowfs_hdl,
			       int fd, attrmask_t request_mask,
			       struct attrlist *attrib)
{
	struct cowfs_acl_entry *fa;

	if (!FSAL_TEST_MASK(request_mask, ATTR_ACL) || !attrib || !attrib->acl)
		return fsalstat(ERR_FSAL_NO_ERROR, 0);

	LogDebug(COMPONENT_FSAL, "Enter");
	fsal_print_acl(COMPONENT_FSAL, NIV_FULL_DEBUG, attrib->acl);
	fa = cowfs_acl_locate(&cowfs_hdl->obj_handle);
	nfs4_ace_free(fa->fa_acl.aces);
	fa->fa_acl.naces = attrib->acl->naces;
	fa->fa_acl.aces = (fsal_ace_t *) nfs4_ace_alloc(fa->fa_acl.naces);
	memcpy(fa->fa_acl.aces, attrib->acl->aces,
	       fa->fa_acl.naces * sizeof(fsal_ace_t));
	fsal_print_acl(COMPONENT_FSAL, NIV_FULL_DEBUG,
		       (fsal_acl_t *)&fa->fa_acl);
	if (attrib->mask & ATTR_MODE)
		cowfs_hdl->mode = attrib->mode;

	FSAL_SET_MASK(attrib->mask, ATTR_ACL);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
