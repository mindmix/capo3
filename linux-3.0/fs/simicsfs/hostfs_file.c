
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

   $Id: hostfs_file.c,v 1.3 2006/03/31 10:25:53 am Exp $
*/

#include "hostfs_linux.h"

static int
hostfs_fo_open(struct inode *inode, struct file *file)
{
        struct hf_common_data idata;
        uint oflag = 0, flag = file->f_flags;

        DPRINT1("hostfs_fo_open %ld flags %x\n", inode->i_ino, file->f_flags);

        switch (flag & O_ACCMODE) {
        case O_RDONLY:
                oflag = HF_FREAD;
                break;
        case O_RDWR:
                oflag = HF_FREAD; 
                /* fallthrough */
        case O_WRONLY:
                oflag |= HF_FWRITE;
                break;
        default:
                return -EINVAL;
        }

        get_host_data(hf_Open, inode->i_ino, &oflag, &idata);

        if (idata.sys_error)
                return -idata.sys_error;

        return 0;
}

static int
hostfs_fo_release(struct inode *inode, struct file *file)
{
        uint dummy;

        /* DPRINT1("hostfs_fo_release %ld fcount %d icount %d\n",
        	(long)inode->i_ino, atomic_read(&file->f_count),
        	atomic_read(&inode->i_count)); */

        get_host_data(hf_Close, inode->i_ino, NULL, &dummy);
        return 0;
}

static ssize_t
hostfs_read(struct file *file, char *buf, size_t len, loff_t *ppos)
{
        DPRINT1("hostfs: hostfs_read %ld\n", file->f_dentry->d_inode->i_ino);
        if (!hostfs_revalidate_inode(file->f_dentry->d_inode))
                return 0;
#if defined(KERNEL_2_6)
        return do_sync_read(file, buf, len, ppos);
#else
        return generic_file_read(file, buf, len, ppos);
#endif
}

static ssize_t
hostfs_write(struct file *file, const char *buf, size_t len, loff_t *ppos)
{
        DPRINT1("hostfs: hostfs_write %ld\n", file->f_dentry->d_inode->i_ino);
        if (!hostfs_revalidate_inode(file->f_dentry->d_inode))
                return 0;
#if defined(KERNEL_2_6)
        return do_sync_write(file, buf, len, ppos);
#else
        return generic_file_write(file, buf, len, ppos);
#endif
}

#if defined(KERNEL_2_6)
 #if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19)
static ssize_t
hostfs_aio_read(struct kiocb *kiocb, const struct iovec *iovec,
                unsigned long len, loff_t ofs)
 #else
static ssize_t
hostfs_aio_read(struct kiocb *kiocb, char *buf, size_t len, loff_t ppos)
 #endif
{
        DPRINT1("hostfs: hostfs_aio_read %ld\n",
                kiocb->ki_filp->f_dentry->d_inode->i_ino);
        if (!hostfs_revalidate_inode(kiocb->ki_filp->f_dentry->d_inode))
                return 0;
        #if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19)
                return generic_file_aio_read(kiocb, iovec, len, ofs);
        #else
                return generic_file_aio_read(kiocb, buf, len, ppos);
        #endif
}

 #if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19)
static ssize_t
hostfs_aio_write(struct kiocb *kiocb, const struct iovec *iovec,
                 unsigned long len, loff_t ofs)
 #else
static ssize_t
hostfs_aio_write(struct kiocb *kiocb, const char *buf, size_t len, loff_t ppos)
 #endif
{
        DPRINT1("hostfs: hostfs_aio_write %ld\n",
                kiocb->ki_filp->f_dentry->d_inode->i_ino);
        if (!hostfs_revalidate_inode(kiocb->ki_filp->f_dentry->d_inode))
                return 0;
        #if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19)
                return generic_file_aio_write(kiocb, iovec, len, ofs);
        #else
                return generic_file_aio_write(kiocb, buf, len, ppos);
        #endif
}
#endif

struct file_operations hostfs_file_fops = {
#if defined(KERNEL_2_6)
        read: hostfs_read,
        write: hostfs_write,
        aio_read: hostfs_aio_read,
        aio_write: hostfs_aio_write,
#else
        read: hostfs_read,
        write: hostfs_write,
#endif
        mmap: generic_file_mmap,
        open: hostfs_fo_open,
	release: hostfs_fo_release,
};
