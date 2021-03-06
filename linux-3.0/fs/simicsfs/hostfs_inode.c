/*
   hostfs for Linux
   Copyright 2001 - 2006 Virtutech AB
   Copyright 2001 SuSE

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
   NON INFRINGEMENT.  See the GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

   $Id: hostfs_inode.c,v 1.5 2006/03/31 10:25:53 am Exp $
*/

#include "hostfs_linux.h"


static struct address_space_operations hostfs_file_aops;
static struct inode_operations hostfs_symlink_iops, hostfs_dir_iops;

static struct inode_operations hostfs_file_iops;

int
hostfs_revalidate_inode(struct inode *inode)
{
#ifdef KERNEL_2_6
        struct inode updated_inode = *inode;

        DPRINT1("hostfs_revalidate %ld\n", (long)inode->i_ino);

        hostfs_read_inode(&updated_inode);

        if (memcmp(&updated_inode, inode, sizeof(updated_inode)) != 0) {
                DPRINT1("hostfs: inode changed, flushing pages\n");
                *inode = updated_inode;

                /* TODO: can we do a better guess when to invalidate pages and
                   when not to? */
                invalidate_inode_pages2(inode->i_mapping);
        } else {
                DPRINT1("hostfs: inode not changed\n");
        }
#endif  /* KERNEL_2_6 */
        return 1;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)
struct inode *
hostfs_iget(struct super_block *sb, unsigned long ino)
{
        struct inode *inode;

        inode = iget_locked(sb, ino);
        if (!inode)
                return ERR_PTR(-ENOMEM);
        if (!(inode->i_state & I_NEW))
                return inode;

        hostfs_read_inode(inode);

        unlock_new_inode(inode);
        return inode;
}
#endif

void
hostfs_read_inode(struct inode *inode)
{
        struct hf_getattr_data data;
        uint mask;

        get_host_data(hf_GetAttr, inode->i_ino, &mask, &data);
        if (data.sys_error) {
                printk(DEVICE_NAME "get_host_data(hf_GetAttr, %ld, ...) "
                       "had an error\n", inode->i_ino);
                return;
        }

        DPRINT1("hostfs_read_inode(%ld) flags %lx\n", inode->i_ino,
                (long)inode->i_flags);

        inode->i_flags = 0;
        inode->i_mode = data.mode;
        inode->i_uid = data.uid;
        inode->i_gid = data.gid;
        inode->i_nlink = data.link;
        GET_HI_LO(inode->i_size,  data.size);
#ifndef KERNEL_2_6
        GET_HI_LO(inode->i_atime, data.atime);
        GET_HI_LO(inode->i_mtime, data.mtime);
        GET_HI_LO(inode->i_ctime, data.ctime);
#else /* KERNEL_2_6 */
        GET_HI_LO(inode->i_atime.tv_sec, data.atime);
        GET_HI_LO(inode->i_mtime.tv_sec, data.mtime);
        GET_HI_LO(inode->i_ctime.tv_sec, data.ctime);
        inode->i_atime.tv_nsec = 0;
        inode->i_mtime.tv_nsec = 0;
        inode->i_ctime.tv_nsec = 0;
#endif /* KERNEL_2_6 */
        #if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 19)
                /* blksize field completely removed in 2.6.19 */
                inode->i_blksize = data.blksize;
        #endif
        GET_HI_LO(inode->i_blocks, data.blocks);

        if (S_ISLNK(inode->i_mode)) {
                inode->i_op = &hostfs_symlink_iops;
        } else if (S_ISDIR(inode->i_mode)) {
                inode->i_op = &hostfs_dir_iops;
		inode->i_fop = &hostfs_file_dirops;
        } else {
		inode->i_fop = &hostfs_file_fops;
		inode->i_op = &hostfs_file_iops;
                inode->i_mapping->a_ops = &hostfs_file_aops;
        }
}

#ifdef KERNEL_3_0
int hostfs_write_inode(struct inode *inode, struct writeback_control *wbc)
#elif defined KERNEL_2_6
int hostfs_write_inode(struct inode *inode, int sync)
#else
void hostfs_write_inode(struct inode *inode, int sync)
#endif
{
        /*
         * it might be pointless to implement this function since
         * supposedly all changes will have been updated by
         * notify_change anyway, but I'm sure it can't hurt
         */

        struct hf_setattr_data odata;
        struct hf_common_data idata;

        DPRINT1(DEVICE_NAME " hostfs_write_inode(%ld)\n", inode->i_ino);

        memset(&odata, 0, sizeof odata);
        odata.set_atime = 1;
#ifndef KERNEL_2_6
        SET_HI_LO(odata.atime, inode->i_atime);
        SET_HI_LO(odata.mtime, inode->i_mtime);
#else /* KERNEL_2_6 */
        SET_HI_LO(odata.atime, inode->i_atime.tv_sec);
        SET_HI_LO(odata.mtime, inode->i_mtime.tv_sec);
#endif /* KERNEL_2_6 */
        odata.set_mtime = 1;
        odata.set_mode = 1;
        odata.mode = inode->i_mode;
        odata.set_uid = 1;
        odata.uid = inode->i_uid;
        odata.set_gid = 1;
        odata.gid = inode->i_gid;
        get_host_data(hf_SetAttr, inode->i_ino, &odata, &idata);
#if (defined KERNEL_2_6)
        return 0;
#endif
}

static int
hostfs_setattr(struct dentry *dentry, struct iattr *iattr)
{
        struct hf_setattr_data odata;
        struct hf_common_data idata;
        struct inode *inode = dentry->d_inode;
        int setsize = 0;
        int error;

        DPRINT1(DEVICE_NAME " hostfs_notify_change(%ld, 0x%lx)\n",
                inode->i_ino, (long)iattr->ia_valid);

        if ((error = inode_change_ok(inode, iattr)))
            return error;

        memset(&odata, 0, sizeof odata);
        if (iattr->ia_valid & ATTR_ATIME) {
            odata.set_atime = 1;
#ifndef KERNEL_2_6
            SET_HI_LO(odata.atime, iattr->ia_atime);
#else /* KERNEL_2_6 */
            SET_HI_LO(odata.atime, iattr->ia_atime.tv_sec);
#endif /* KERNEL_2_6 */
        }
        if (iattr->ia_valid & ATTR_MTIME) {
            odata.set_mtime = 1;
#ifndef KERNEL_2_6
            SET_HI_LO(odata.mtime, iattr->ia_mtime);
#else /* KERNEL_2_6 */
            SET_HI_LO(odata.mtime, iattr->ia_mtime.tv_sec);
#endif /* KERNEL_2_6 */
        }
        if (iattr->ia_valid & ATTR_MODE) {
            odata.set_mode = 1;
            odata.mode = iattr->ia_mode;
        }
        if (iattr->ia_valid & ATTR_UID) {
            odata.set_uid = 1;
            odata.uid = iattr->ia_uid;
        }
        if (iattr->ia_valid & ATTR_GID) {
            odata.set_gid = 1;
            odata.gid = iattr->ia_gid;
        }
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36)
        if (iattr->ia_valid & ATTR_SIZE && iattr->ia_size != inode->i_size) {
            setsize = 1;
            odata.set_size = 1;
            SET_HI_LO(odata.size, iattr->ia_size);
        }
#endif
        get_host_data(hf_SetAttr, inode->i_ino, &odata, &idata);

        DPRINT1("         hf_SetAttr returned %d\n", idata.sys_error);

        if (idata.sys_error)
            return -idata.sys_error;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36)
        if (setsize)
            truncate_setsize(inode, iattr->ia_size);
        setattr_copy(inode, iattr);
        mark_inode_dirty(inode);
        return 0;
#else
        return inode_setattr(inode, iattr);
#endif
}

#ifdef KERNEL_2_6
static int
hostfs_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *stat)
{
        DPRINT1("hostfs: getattr\n");
        if (!hostfs_revalidate_inode(dentry->d_inode))
                return 0;
        generic_fillattr(dentry->d_inode, stat);
        return 0;
}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28)
static int
hostfs_write_begin(struct file *file, struct address_space *mapping,
                   loff_t pos, unsigned len, unsigned flags,
                   struct page **pagep, void **fsdata)
{
        pgoff_t index = pos >> PAGE_CACHE_SHIFT;
        struct page *page = grab_cache_page_write_begin(mapping, index, flags);

        *pagep = page;
        if (page == NULL)
                return -ENOMEM;

        return 0;
}
#else
static int
hostfs_prepare_write(struct file *file, struct page *page, unsigned offset,
                     unsigned to)
{
	return 0;
}
#endif

static int
hostfs_do_writepage(struct inode *inode, struct page *page, unsigned from,
                    unsigned to)
{
	int offset = (page->index << PAGE_CACHE_SHIFT) + from;
	unsigned bytes = to - from;
	unsigned long int address = (unsigned long)page_address(page) + from;
	uint ino = inode->i_ino;
	int res;

	if (page->index > (0x7fffffff/PAGE_CACHE_SIZE)-1)
		return -EFBIG;

        spin_lock(&hostfs_position_lock);

        DPRINT1("hostfs_do_writepage: offset:%u bytes:%u address:%lx\n",
                offset, bytes, address);

        hf_do_seek(ino, offset);

	res = 0;
        while (bytes > 0) {
                struct hf_write_data odata;
                struct hf_common_data idata;
                uint cnt = MIN(bytes, XFER_DATA_WORDS * 4);
                odata.append = 0;
                odata.size = cnt;
                memcpy(odata.data, (char *)address, cnt);
                HTONSWAP(odata.data);
                get_host_data(hf_Write, ino, &odata, &idata);
                if (idata.sys_error) {
                        res = -idata.sys_error;
                        DPRINT1(DEVICE_NAME " failed with errcode %d\n", res);
                        SetPageError(page);
			ClearPageUptodate(page);
                        goto got_error;
                }
                address += cnt;
                bytes -= cnt;
		res += cnt;
        }
        DPRINT1(DEVICE_NAME "wrote %d bytes\n", (int)res);

	SetPageUptodate(page);

 got_error:
        spin_unlock(&hostfs_position_lock);

        return res;
}

static int
#ifndef KERNEL_2_6
hostfs_writepage(struct page *page)
#else /* KERNEL_2_6 */
hostfs_writepage(struct page *page, struct writeback_control *wbc)
#endif /* KERNEL_2_6 */
{
	struct inode *inode = page->mapping->host;
	int err = -EIO;
	int bytes = PAGE_CACHE_SIZE;
	unsigned long end_index = inode->i_size >> PAGE_CACHE_SHIFT;
	if (page->index <= end_index) {
		if (page->index == end_index)
			bytes = inode->i_size & (PAGE_CACHE_SIZE-1);
		kmap(page);
		err = hostfs_do_writepage(inode, page, 0, bytes);
		kunmap(page);
	}
#ifndef KERNEL_2_6
	UnlockPage(page);
#else /* KERNEL_2_6 */
 #if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
        unlock_page(page);
 #else
	ClearPageLocked(page);
 #endif
#endif /* KERNEL_2_6 */
	return err;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28)
static int
hostfs_write_end(struct file *file, struct address_space *mapping,
                 loff_t pos, unsigned len, unsigned copied,
                 struct page *page, void *fsdata)
{
        unsigned from = pos & (PAGE_CACHE_SIZE - 1);
        unsigned to = from + copied;

	struct inode *inode = mapping->host;
	int err;

        kmap(page);
	err = hostfs_do_writepage(inode, page, from, to);

	if (err >= 0) {
		loff_t s = ((page->index) << PAGE_CACHE_SHIFT) + to;
		if (s > inode->i_size) {
			inode->i_size = s;
                }
	}

	kunmap(page);

        unlock_page(page);
        page_cache_release(page);
	return err;
}

#else

static int
hostfs_commit_write(struct file *file, struct page *page, unsigned from,
                    unsigned to)
{
	struct inode *inode = page->mapping->host;
	int err;

        kmap(page);
	err = hostfs_do_writepage(inode, page, from, to);
	kunmap(page);

	if (err < 0)
		return err;

	{
		loff_t s = ((page->index) << PAGE_CACHE_SHIFT) + to;
		if (s > inode->i_size)
			inode->i_size = s;
	}
	return 0;
}
#endif

static int
hostfs_inode_readpage(struct file *file, struct page *page)
{
        char *address;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)
        struct inode *inode = file->f_path.dentry->d_inode;
#else
        struct inode *inode = file->f_dentry->d_inode;
#endif
        uint count = PAGE_CACHE_SIZE;
        int err;

	if (page->index > (0xffffffff/PAGE_CACHE_SIZE)-1)
		return -EFBIG;
	ClearPageError(page);
        count = PAGE_SIZE;

/*          DPRINT1("readpage seek %ld\n", (long)page->offset); */

	address = kmap(page);

        spin_lock(&hostfs_position_lock);

        if ((err = hf_do_seek(inode->i_ino,
                              page->index << PAGE_CACHE_SHIFT))) {
                err = -err;
                goto out_error;
        }

        while (count > 0) {
                struct hf_read_data data;
                uint to_read = MIN(count, XFER_DATA_WORDS * 4);

                get_host_data(hf_Read, inode->i_ino, &to_read, &data);
                if (data.sys_error) {
                        err = -data.sys_error;
                        goto out_error;
                }

/*              DPRINT1("readpage count %d out of %d\n", data.size, count); */

		if (!data.size) {
			memset(address,0,count);
			break;
		}

                NTOHSWAP(data.data);

                memcpy(address, data.data, data.size);


                address += data.size;
                count -= data.size;
        }

        err = 0;
        spin_unlock(&hostfs_position_lock);

	SetPageUptodate(page);
out_error:
        if (err)
                SetPageError(page);
#ifndef KERNEL_2_6
        UnlockPage(page);
#else /* KERNEL_2_6 */
 #if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
        unlock_page(page);
 #else
	ClearPageLocked(page);
 #endif
#endif /* KERNEL_2_6 */
        kunmap(page);

/*          DPRINT1("postuse %ld\n", (long)atomic_read(&page->count)); */

        return err;
}

static int
hostfs_inode_readlink(struct dentry *dentry, char *buf, int size)
{
	struct hf_readlink_data data;

	get_host_data(hf_Readlink, dentry->d_inode->i_ino, NULL, &data);
	if (data.sys_error)
		return -data.sys_error;
	NTOHSWAP(data.filename);
	return vfs_readlink(dentry, buf, size, data.filename);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,13)
static void *
#else
static int
#endif
hostfs_inode_follow_link(struct dentry *dentry, struct nameidata *nd)
{
/*       DPRINT1("hostfs_inode_follow_link ino %ld %ld %d\n",
                 base->d_inode->i_ino, dentry->d_inode->i_ino, follow); */
	struct hf_readlink_data data;
	char *name;
        int ret_val;

	get_host_data(hf_Readlink, dentry->d_inode->i_ino, NULL, &data);
	if (data.sys_error)
		name = ERR_PTR(-data.sys_error);
	else {
		name = data.filename;
		NTOHSWAP(data.filename);
	}

        ret_val = vfs_follow_link(nd, data.filename);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,13)
        return ERR_PTR(ret_val);
#else
        return ret_val;
#endif
}

static struct dentry *
#ifndef KERNEL_2_6
hostfs_inode_lookup(struct inode *dir, struct dentry *dentry)
#else /* KERNEL_2_6 */
hostfs_inode_lookup(struct inode *dir, struct dentry *dentry,
                    struct nameidata *name_data)
#endif /* KERNEL_2_6 */
{
        struct hf_lookup_data odata;
        struct hf_hnode_data idata;
        struct inode *inode;

/*          DPRINT1("hostfs_inode_lookup(%ld, dent %p = %s)\n", dir->i_ino, dentry, dentry->d_name.name); */

	/* protect lots of stupid strcpy()s */
	if (dentry->d_name.len > HOSTFS_FILENAME_LEN-1)
		return ERR_PTR(-ENAMETOOLONG);

	memcpy(odata.filename, dentry->d_name.name, dentry->d_name.len);
	odata.filename[dentry->d_name.len] = 0;
	HTONSWAP(odata.filename);
	get_host_data(hf_Lookup, dir->i_ino, &odata, &idata);
 
        if (idata.sys_error) {
                if (idata.sys_error != ENOENT)
                        return ERR_PTR(-idata.sys_error);
                inode = NULL;
        } else {
                #if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)
                        inode = hostfs_iget(dir->i_sb, idata.hnode);
                #else
                        inode = iget(dir->i_sb, idata.hnode);
                #endif
        }

        d_add(dentry, inode);

        return ERR_PTR(0);
}

static int
hostfs_inode_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
        struct hf_mkdir_data odata;
        struct hf_hnode_data idata;
        struct inode *inode;

        DPRINT1("hostfs_inode_mkdir()\n");

        odata.mode = mode;
        strcpy(odata.dirname, dentry->d_name.name);
        HTONSWAP(odata.dirname);

        get_host_data(hf_MkDir, dir->i_ino, &odata, &idata);
        if (idata.sys_error)
                return -idata.sys_error;;

        #if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)
                inode = hostfs_iget(dir->i_sb, idata.hnode);
        #else
                inode = iget(dir->i_sb, idata.hnode);
                hostfs_read_inode(inode);
        #endif
        mark_inode_dirty(inode);
        d_instantiate(dentry, inode);

        return 0;
}

static int
hostfs_inode_rmdir(struct inode *dir, struct dentry *dentry)
{
        struct hf_remove_data odata;
        struct hf_common_data idata;

        DPRINT1("hostfs_inode_rmdir()\n");

        strcpy(odata.filename, dentry->d_name.name);
        HTONSWAP(odata.filename);
        get_host_data(hf_Remove, dir->i_ino, &odata, &idata);
        mark_inode_dirty(dir);

        return -idata.sys_error;
}

static int
#ifndef KERNEL_2_6
hostfs_inode_create(struct inode *dir, struct dentry *dentry, int mode)
#else /* KERNEL_2_6 */
hostfs_inode_create(struct inode *dir, struct dentry *dentry, int mode,
                    struct nameidata *name_data)
#endif /* KERNEL_2_6 */
{
        struct hf_create_data odata;
        struct hf_hnode_data idata;
        struct inode *inode;

        DPRINT1("hostfs_inode_create(%ld, mode = %d)\n", dir->i_ino, mode);

        odata.mode = mode & S_IALLUGO;
        odata.excl = 0;
        odata.trunc = 0;
        strncpy(odata.filename, dentry->d_name.name, sizeof(odata.filename));
        HTONSWAP(odata.filename);

        get_host_data(hf_Create, dir->i_ino, &odata, &idata);

        if (idata.sys_error) {
                return -idata.sys_error;
        }

        #if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)
                inode = hostfs_iget(dir->i_sb, idata.hnode);
        #else
                inode = iget(dir->i_sb, idata.hnode);
                hostfs_read_inode(inode);
        #endif
        DPRINT1("    read inode mode 0%o  uid %d\n", inode->i_mode,
                inode->i_uid);
        mark_inode_dirty(inode);
        d_instantiate(dentry, inode);

        return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36)
static void
hostfs_inode_truncate(struct inode *inode)
{
        struct hf_setattr_data odata;
        struct hf_common_data idata;

        DPRINT1("hostfs_inode_truncate(inode = %ld)\n", inode->i_ino);

        memset(&odata, 0, sizeof odata);
        odata.set_size = 1;
        SET_HI_LO(odata.size, inode->i_size);
        get_host_data(hf_SetAttr, inode->i_ino, &odata, &idata);

        if (idata.sys_error) {
                printk("[hostfs] call to truncate() failed with error %d!\n",
                       idata.sys_error);
        }
}
#endif

static int
hostfs_unlink(struct inode *dir, struct dentry *dentry)
{
        struct hf_remove_data odata;
        struct hf_common_data idata;

        DPRINT1("hostfs_unlink(dir = %ld, dentry = %s)\n", dir->i_ino,
                dentry->d_name.name);

        strcpy(odata.filename, dentry->d_name.name);
        HTONSWAP(odata.filename);
        get_host_data(hf_Remove, dir->i_ino, &odata, &idata);

        return -idata.sys_error;
}

static struct inode_operations hostfs_dir_iops = {
        create: hostfs_inode_create,
        lookup: hostfs_inode_lookup,
        unlink: hostfs_unlink,
        mkdir: hostfs_inode_mkdir,
        rmdir: hostfs_inode_rmdir,
};

static struct inode_operations hostfs_symlink_iops = {
        readlink: hostfs_inode_readlink,
        follow_link: hostfs_inode_follow_link,
};

static struct inode_operations hostfs_file_iops = {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36)
        truncate: hostfs_inode_truncate,
#endif
	setattr: hostfs_setattr,
#ifdef KERNEL_2_6
        getattr: hostfs_getattr,
#endif
};

static struct address_space_operations hostfs_file_aops = {
	readpage: hostfs_inode_readpage,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28)
        write_begin: hostfs_write_begin,
        write_end: hostfs_write_end,
#else
	prepare_write: hostfs_prepare_write,
	commit_write: hostfs_commit_write,
#endif
	writepage: hostfs_writepage,
};

MODULE_LICENSE("GPL");
