/* Copyright (c) 2020, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include<linux/init.h>
#include<linux/kernel.h>
#include<linux/module.h>

#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ids.h>

#include <linux/cdev.h>
#include <linux/delay.h>

#include <linux/slab.h>
#include <linux/uaccess.h>
#include <soc/qcom/boot_stats.h>
#include "eavb_shared.h"

#include "vio_eavb.h"


//TODO need add VIRTIO_ID_EAVB in uapi/linux/virtio_ids.h
#define VIRTIO_ID_EAVB 		35

//support feature
#define VIRTIO_EAVB_F_SHMEM	1


#define MINOR_NUM_DEV 		0
#define DEVICE_NAME 		"virt-eavb"
#define DEVICE_NUM 		1


#define MODULE_SETUP 	(1<<0)
#define MODULE_RX    	(1<<1)
#define MODULE_TX    	(1<<2)
#define MODULE_ALL   	(MODULE_SETUP|MODULE_RX|MODULE_TX)

#define LEVEL_DEBUG 1
#define LEVEL_INFO 2
#define LEVEL_ERR 3

static unsigned int log_level = LEVEL_INFO;
static unsigned int log_module = MODULE_ALL;
module_param(log_level, uint, S_IRUGO);
module_param(log_module, uint, S_IRUGO);

char* prix [] = {"", "debug", "info", "error"};
#define LOG_EAVB(modle, level, format, args...) \
do { \
	if (((modle) & log_module) && ((level) >= log_level)) { \
		printk("pid %.8x: %s: %s(%d) "format, current->pid, prix[0x3 & (level)], __func__, __LINE__, ## args); \
	} \
} while(0)

#define ASSERT(x) \
do { \
	if (unlikely(!(x))) { \
		printk(KERN_ERR "Assertion failed! %s %s:%d\n", __func__, __FILE__, __LINE__); \
		dump_stack(); \
	} \
} while (0)

#define eavb_place_marker_once(log) \
do {\
        static int times = 0; \
        if (times < 1) {\
                place_marker(log); \
                times++; \
        } \
} while(0)

struct fe_msg;
struct virtio_eavb_priv {
	char *name;
	struct virtio_device *vdev;
	struct virtqueue     *svq;
	struct virtqueue     *rvq;
	spinlock_t rvqlock;

	bool has_shmem;

	struct mutex lock;

	struct device *dev;
	struct cdev cdev;
	struct class *class;
	dev_t dev_no;
	int file_index;

	spinlock_t msglock;
#define FE_MSG_MAX 		256 //1<<8
#define mod(index) 		((index) & 0xff)  //((index) % FE_MSG_MAX)
	struct fe_msg *msgtable[FE_MSG_MAX];
#define RX_BUF_MAX_LEN 		1024
	void *rxbufs[FE_MSG_MAX];
};

struct mapping {
	struct list_head list;
	uint64_t va; //kernel virtual address
	uint64_t ua; //user address
	uint64_t pa; //phy address
	size_t size;
};

struct eavb_file {
	int index;
	struct virtio_eavb_priv *priv;
#define MAPPING_MAX		8
	int mapping_count;
	struct mapping mapping[MAPPING_MAX];
	spinlock_t mappinglock;

	struct stream {
		uint64_t hdl;
		int32_t idx;
		enum stream_status {
			UNCREATED = 0,
			CREATED = 1,
			CONNECTED = 2,
			DISCONNECTED = 3,
			DESTROYED = 4,
		} status;
	} stream[MAX_STREAM_NUM];
	int stream_count;
	spinlock_t streamlock;
};

int getStreamIndex(struct eavb_file *fl, uint64_t hdl)
{
	int i;

	for (i=0; i<MAX_STREAM_NUM; i++) {
		if (fl->stream[i].hdl == hdl) {
			break;
		}
	}
	if (i < MAX_STREAM_NUM) {
		if (hdl == 0) {
			fl->stream[i].hdl = 0xffffffffffffffff;
		}
	}

	return i;
}

int get_mapping_idx(struct eavb_file *fl, uint64_t addr, int64_t len)
{
	uint64_t start, end;
	int i;

	for (i=0; i<MAPPING_MAX; i++) {
		start = fl->mapping[i].ua;
		end = fl->mapping[i].ua + fl->mapping[i].size;
		if (addr >= start && addr + len <= end) break;
	}
	return i;
}
uint64_t get_mapping_phyaddr(struct eavb_file *fl, uint64_t ua, int idx)
{
	if (idx < MAPPING_MAX && idx >= 0)
	{
		return fl->mapping[idx].pa + (ua - fl->mapping[idx].ua);
	}
	return 0;
}

int add_mapping_info(struct eavb_file *fl, uint64_t va, uint64_t ua, uint64_t pa, size_t size)
{
	int i;

	for (i=0; i<MAPPING_MAX; i++) {
		if (0 == fl->mapping[i].va) {
			fl->mapping[i].size = size;
			fl->mapping[i].va = va;
			fl->mapping[i].ua = ua;
			fl->mapping[i].pa = pa;
			break;
		}
	}
	return i;
}


struct fe_msg {
	u16 msgid;
	struct completion work;
	void *txbuf;
	void *rxbuf;
	u32 txbuf_size;
	u32 rxbuf_size;
	u32 txbuf_used;
	u32 rxbuf_used;
};

static struct fe_msg *virt_alloc_msg(struct virtio_eavb_priv *priv, int tsize, int rsize)
{
	struct fe_msg *msg;
	unsigned long flags;
	u8 *buf;
	int i;
	static int next = 0;

	ASSERT(rsize < RX_BUF_MAX_LEN);
	//kzalloc needn't memset 0
	msg = kzalloc(sizeof(*msg) + tsize, GFP_KERNEL);
	if (!msg) {
		LOG_EAVB(MODULE_TX, LEVEL_ERR, "cannot alloc memory\n");
		return NULL;
	}

	spin_lock_irqsave(&priv->msglock, flags);
	for (i = 0; i < FE_MSG_MAX; i++) {
		if (!priv->msgtable[mod(next + i)]) {
			priv->msgtable[mod(next + i)] = msg;
			msg->msgid = mod(next + i);
			break;
		}
	}
	spin_unlock_irqrestore(&priv->msglock, flags);

	if (i == FE_MSG_MAX) {
		LOG_EAVB(MODULE_TX, LEVEL_ERR, "fe message queue is full \n");
		kfree(msg);
		return NULL;
	}

	buf = (u8 *)(msg + 1);
	msg->txbuf = buf;
	msg->txbuf_size = tsize;
	init_completion(&msg->work);

	next = mod(msg->msgid + 1);
	return msg;
}

static void virt_free_msg(struct virtio_eavb_priv *priv, struct fe_msg *msg)
{
	unsigned long flags;

	if (msg->txbuf_used && msg->rxbuf_used) {
		struct scatterlist sg;
		u8 *rsp = msg->rxbuf;

		sg_init_one(&sg, rsp, RX_BUF_MAX_LEN);
		spin_lock_irqsave(&priv->rvqlock, flags);
		virtqueue_add_inbuf(priv->rvq, &sg, 1, rsp, GFP_KERNEL);
		virtqueue_kick(priv->rvq);
		spin_unlock_irqrestore(&priv->rvqlock, flags);

		spin_lock_irqsave(&priv->msglock, flags);
		if (priv->msgtable[msg->msgid] == msg)
			priv->msgtable[msg->msgid] = NULL;
		else
			LOG_EAVB(MODULE_TX, LEVEL_ERR, "can't find msg %d in table\n", msg->msgid);
		spin_unlock_irqrestore(&priv->msglock, flags);

		kfree(msg);
	} else {
		LOG_EAVB(MODULE_TX, LEVEL_DEBUG, "msgid %d (tx %d, rx %d) buf not used\n", msg->msgid, msg->txbuf_used, msg->rxbuf_used);
	}
}

static int send_msg(struct virtio_eavb_priv *priv, struct fe_msg *msg)
{
	struct vio_msg_hdr *vhdr, *rsp;
	struct scatterlist sg[1];
	int ret;
	int rsv;
	unsigned int len;

	vhdr = (struct vio_msg_hdr *)msg->txbuf;
	msg->rxbuf = NULL;

	LOG_EAVB(MODULE_TX, LEVEL_DEBUG, "msgid %d\n", msg->msgid);
	mutex_lock(&priv->lock);
	vhdr->msgid = msg->msgid;
	sg_init_one(sg, vhdr, vhdr->len);
	ret = virtqueue_add_outbuf(priv->svq, sg, 1, vhdr, GFP_KERNEL);
	if (ret) {
		LOG_EAVB(MODULE_TX, LEVEL_ERR, "fail to add output buffer, return (%d)\n", ret);
		mutex_unlock(&priv->lock);
		return ret;
	}
	virtqueue_kick(priv->svq);
	mutex_unlock(&priv->lock);

	rsv = wait_for_completion_timeout(&msg->work, msecs_to_jiffies(1000));
	if (rsv == 0) {
		LOG_EAVB(MODULE_TX, LEVEL_ERR, "msgid %d timeout\n", msg->msgid);
		return -ETIMEDOUT;
	}

	if ( 0 == msg->txbuf_used) {
		while (1) {
			struct fe_msg *used;

			mutex_lock(&priv->lock);
			vhdr = virtqueue_get_buf(priv->svq, &len); //remove a used txbuf
			mutex_unlock(&priv->lock);

			if (!vhdr) break;

			ASSERT(vhdr->len == len);
			used = priv->msgtable[vhdr->msgid];
			if (!used) {
				LOG_EAVB(MODULE_TX, LEVEL_ERR, "msgid %d used txbuf found no msg\n", vhdr->msgid);
				continue;
			}
			ASSERT(used->msgid == vhdr->msgid);
			used->txbuf_used = 1;
			LOG_EAVB(MODULE_TX, LEVEL_DEBUG, "msgid %d txbuf used\n", used->msgid);
		}
	}

	if (0 == msg->txbuf_used) {
		LOG_EAVB(MODULE_TX, LEVEL_ERR, "msgid %d txbuf not used\n", msg->msgid);
		ret = -1;
	}

	rsp = msg->rxbuf;
	if (rsp) {
		LOG_EAVB(MODULE_TX, LEVEL_DEBUG, "msgid %d, result = %d\n", msg->msgid, rsp->result);
	}
	return ret;
}

static int virtio_eavb_open(struct inode *inode, struct file *file)
{
	struct cdev *cdev = inode->i_cdev;
	struct virtio_eavb_priv *priv = container_of(cdev, struct virtio_eavb_priv, cdev);
	struct eavb_file *fl;

	fl = kzalloc(sizeof(*fl), GFP_KERNEL);
	if (!fl) {
		LOG_EAVB(MODULE_SETUP, LEVEL_ERR, "malloc fail\n");
		return -ENOMEM;
	}

	memset(fl, 0, sizeof(*fl));
	fl->priv = priv;
	file->private_data = fl;
	spin_lock_init(&fl->mappinglock);
	spin_lock_init(&fl->streamlock);

	fl->index = priv->file_index++;
	LOG_EAVB(MODULE_SETUP, LEVEL_INFO, "fl->index=%d\n", fl->index);
	return 0;
}

static int virtio_eavb_release(struct inode *inode, struct file *file)
{
	struct eavb_file *fl = file->private_data;
	struct virtio_eavb_priv *priv = fl->priv;
	int i;

	LOG_EAVB(MODULE_SETUP, LEVEL_INFO, "fl->index=%d\n", fl->index);
	for (i=0; i<MAX_STREAM_NUM; i++) {
		uint64_t hdl = fl->stream[i].hdl;
		int32_t idx = fl->stream[i].idx;
		int status = fl->stream[i].status;
		struct fe_msg *msg;
		int tsize, rsize;

		LOG_EAVB(MODULE_SETUP, LEVEL_INFO, "[%d], hdl %llx, idx %d, status %d\n", i, hdl, idx, status);
		switch(status)
		{
			case CONNECTED:

			tsize = rsize = sizeof(struct vio_disconnect_stream_msg);
			msg = virt_alloc_msg(priv, tsize, rsize);
			if (msg) {
				struct vio_msg_hdr *vhdr;

				vhdr = (struct vio_msg_hdr *)msg->txbuf;
				vhdr->cmd = VIRTIO_EAVB_T_DISCONNECT_STREAM;
				vhdr->len = msg->txbuf_size;
				vhdr->streamctx_hdl = fl->stream[i].hdl;
				vhdr->stream_idx = fl->stream[i].idx;

				send_msg(priv, msg);
				virt_free_msg(priv, msg);
			}

			case DISCONNECTED:
			case CREATED:

			tsize = rsize = sizeof(struct vio_destroy_stream_msg);
			msg = virt_alloc_msg(priv, tsize, rsize);
			if (msg) {
				struct vio_msg_hdr *vhdr;

				vhdr = (struct vio_msg_hdr *)msg->txbuf;
				vhdr->cmd = VIRTIO_EAVB_T_DESTROY_STREAM;
				vhdr->len = msg->txbuf_size;
				vhdr->streamctx_hdl = fl->stream[i].hdl;
				vhdr->stream_idx = fl->stream[i].idx;

				send_msg(priv, msg);
				virt_free_msg(priv, msg);
			}
		}
	}

	file->private_data = NULL;
	for (i=0; i<MAPPING_MAX; i++) {
		if (fl->mapping[i].va) {
			kfree((void *)fl->mapping[i].va);
		}
	}
	memset(fl->mapping, 0, sizeof(fl->mapping));
	fl->mapping_count = 0;

	LOG_EAVB(MODULE_SETUP, LEVEL_INFO, "fl->index=%d\n", fl->index);
	kfree(fl);
	return 0;
}

static int qavb_create_stream(struct eavb_file *fl, void __user *buf)
{
	struct virtio_eavb_priv *priv = fl->priv;
	struct fe_msg *msg;
	struct vio_msg_hdr *vhdr;
	struct vio_create_stream_msg *vmsg;
	struct eavb_ioctl_create_stream create;
	int tsize, rsize;
	int index;
	int ret;

	copy_from_user(&create, buf, sizeof(create));
	LOG_EAVB(MODULE_SETUP, LEVEL_INFO, "fl->index=%d\n", fl->index);

	spin_lock(&fl->streamlock);
	index = getStreamIndex(fl, 0);
	spin_unlock(&fl->streamlock);
	if (index >= MAX_STREAM_NUM) {
		LOG_EAVB(MODULE_SETUP, LEVEL_ERR, "stream full\n");
		return -EINVAL;
	}

	tsize = rsize = sizeof(struct vio_create_stream_msg);
	msg = virt_alloc_msg(priv, tsize, rsize);
	if (!msg) {
		LOG_EAVB(MODULE_SETUP, LEVEL_ERR, "stream%d alloc msg fail!\n", index);
		return -ENOMEM;
	}

	vhdr = (struct vio_msg_hdr *)msg->txbuf;
	vhdr->cmd = VIRTIO_EAVB_T_CREATE_STREAM;
	vhdr->len = msg->txbuf_size;

	ASSERT(sizeof(struct eavb_ioctl_stream_config) == sizeof(struct vio_stream_config));
	vmsg = (struct vio_create_stream_msg *)vhdr;
	memcpy(&vmsg->cfg, &create.config, sizeof(struct vio_stream_config));

	LOG_EAVB(MODULE_SETUP, LEVEL_DEBUG, "(%d:%d:%d)\n", fl->index, index, msg->msgid);
	ret = send_msg(priv, msg);
	LOG_EAVB(MODULE_SETUP, LEVEL_DEBUG, "(%d:%d:%d) ret=%d\n", fl->index, index, msg->msgid, ret);

	vhdr = (struct vio_msg_hdr *)msg->rxbuf;
	if (!ret && vhdr) {
		ret = vhdr->result;
		vmsg = (struct vio_create_stream_msg *)vhdr;

		create.hdr.streamCtx = vhdr->streamctx_hdl;

		fl->stream[index].hdl = vhdr->streamctx_hdl;
		fl->stream[index].idx = vhdr->stream_idx;
		fl->stream[index].status = CREATED;
		LOG_EAVB(MODULE_SETUP, LEVEL_INFO, "stream%d (ctx 0x%llx, idx %d)\n", index, vhdr->streamctx_hdl, vhdr->stream_idx);
	}

	virt_free_msg(priv, msg);

	copy_to_user(buf, &create, sizeof(create));

	return ret;
}

static int qavb_create_stream_with_path(struct eavb_file *fl, void __user *buf)
{
	struct virtio_eavb_priv *priv = fl->priv;
	struct fe_msg *msg;
	struct vio_msg_hdr *vhdr;
	struct vio_create_stream_path_msg *vmsg;
	struct eavb_ioctl_create_stream_with_path create;
	int tsize, rsize;
	int index;
	int ret;

	copy_from_user(&create, buf, sizeof(create));
	LOG_EAVB(MODULE_SETUP, LEVEL_INFO, "fl->index=%d\n", fl->index);

	spin_lock(&fl->streamlock);
	index = getStreamIndex(fl, 0);
	spin_unlock(&fl->streamlock);
	if (index >= MAX_STREAM_NUM) {
		LOG_EAVB(MODULE_SETUP, LEVEL_ERR, "stream full\n");
		return -EINVAL;
	}

	tsize = rsize = sizeof(struct vio_create_stream_path_msg);
	msg = virt_alloc_msg(priv, tsize, rsize);
	if (!msg) {
		LOG_EAVB(MODULE_SETUP, LEVEL_ERR, "stream%d alloc msg fail!\n", index);
		return -ENOMEM;
	}

	vhdr = (struct vio_msg_hdr *)msg->txbuf;
	vhdr->cmd = VIRTIO_EAVB_T_CREATE_STREAM_PATH;
	vhdr->len = msg->txbuf_size;

	vmsg = (struct vio_create_stream_path_msg *)vhdr;
	memcpy(vmsg->path, create.path, MAX_CONFIG_FILE_PATH);

	LOG_EAVB(MODULE_SETUP, LEVEL_DEBUG, "(%d:%d:%d)\n", fl->index, index, msg->msgid);
	ret = send_msg(priv, msg);
	LOG_EAVB(MODULE_SETUP, LEVEL_DEBUG, "(%d:%d:%d) ret=%d\n", fl->index, index, msg->msgid, ret);

	vhdr = (struct vio_msg_hdr *)msg->rxbuf;
	if (!ret && vhdr) {
		ret = vhdr->result;
		vmsg = (struct vio_create_stream_path_msg *)vhdr;

		create.hdr.streamCtx = vhdr->streamctx_hdl;

		fl->stream[index].hdl = vhdr->streamctx_hdl;
		fl->stream[index].idx = vhdr->stream_idx;
		fl->stream[index].status = CREATED;
		LOG_EAVB(MODULE_SETUP, LEVEL_INFO, "stream%d (ctx 0x%llx, idx %d)\n", index, vhdr->streamctx_hdl, vhdr->stream_idx);
	}

	virt_free_msg(priv, msg);

	copy_to_user(buf, &create, sizeof(create));

	return ret;
}

static int qavb_destroy_stream(struct eavb_file *fl, void __user *buf)
{
	struct virtio_eavb_priv *priv = fl->priv;
	struct fe_msg *msg;
	struct vio_msg_hdr *vhdr;
	struct eavb_ioctl_destroy_stream destroy;
	int tsize, rsize;
	int index;
	int ret;

	copy_from_user(&destroy, buf, sizeof(destroy));

	spin_lock(&fl->streamlock);
	index = getStreamIndex(fl, destroy.hdr.streamCtx);
	spin_unlock(&fl->streamlock);
	if (index >= MAX_STREAM_NUM) {
		LOG_EAVB(MODULE_SETUP, LEVEL_ERR, "invalid streamCtx=0x%llx\n", destroy.hdr.streamCtx);
		return -EINVAL;
	}

	tsize = rsize = sizeof(struct vio_destroy_stream_msg);
	msg = virt_alloc_msg(priv, tsize, rsize);
	if (!msg) {
		LOG_EAVB(MODULE_SETUP, LEVEL_ERR, "stream%d alloc msg fail!\n", index);
		return -ENOMEM;
	}

	vhdr = (struct vio_msg_hdr *)msg->txbuf;
	vhdr->cmd = VIRTIO_EAVB_T_DESTROY_STREAM;
	vhdr->len = msg->txbuf_size;
	vhdr->streamctx_hdl = fl->stream[index].hdl;
	vhdr->stream_idx = fl->stream[index].idx;

	LOG_EAVB(MODULE_SETUP, LEVEL_DEBUG, "(%d:%d:%d)\n", fl->index, index, msg->msgid);
	ret = send_msg(priv, msg);
	LOG_EAVB(MODULE_SETUP, LEVEL_DEBUG, "(%d:%d:%d) ret=%d\n", fl->index, index, msg->msgid, ret);

	vhdr = (struct vio_msg_hdr *)msg->rxbuf;
	if (!ret && vhdr) {
		ret = vhdr->result;

		fl->stream[index].hdl = 0;
		fl->stream[index].idx = 0;
		fl->stream[index].status = DESTROYED;
	}

	virt_free_msg(priv, msg);

	return ret;
}

static int qavb_get_stream_info(struct eavb_file *fl, void __user *buf)
{
	struct virtio_eavb_priv *priv = fl->priv;
	struct fe_msg *msg;
	struct vio_msg_hdr *vhdr;
	struct eavb_ioctl_get_stream_info get_info;
	int tsize, rsize;
	int index;
	int ret;

	copy_from_user(&get_info, buf, sizeof(get_info));
	LOG_EAVB(MODULE_SETUP, LEVEL_INFO, "streamCtx=0x%llx\n", get_info.hdr.streamCtx);

	spin_lock(&fl->streamlock);
	index = getStreamIndex(fl, get_info.hdr.streamCtx);
	spin_unlock(&fl->streamlock);
	if (index >= MAX_STREAM_NUM) {
		LOG_EAVB(MODULE_SETUP, LEVEL_ERR, "invalid streamCtx=0x%llx\n", get_info.hdr.streamCtx);
		return -EINVAL;
	}

	tsize = rsize = sizeof(struct vio_get_stream_info_msg);
	msg = virt_alloc_msg(priv, tsize, rsize);
	if (!msg) {
		LOG_EAVB(MODULE_SETUP, LEVEL_ERR, "stream%d alloc msg fail!\n", index);
		return -ENOMEM;
	}

	vhdr = (struct vio_msg_hdr *)msg->txbuf;
	vhdr->cmd = VIRTIO_EAVB_T_GET_STREAM_INFO;
	vhdr->len = msg->txbuf_size;
	vhdr->streamctx_hdl = fl->stream[index].hdl;
	vhdr->stream_idx = fl->stream[index].idx;

	LOG_EAVB(MODULE_SETUP, LEVEL_DEBUG, "(%d:%d:%d)\n", fl->index, index, msg->msgid);
	ret = send_msg(priv, msg);
	LOG_EAVB(MODULE_SETUP, LEVEL_DEBUG, "(%d:%d:%d) ret=%d\n", fl->index, index, msg->msgid, ret);

	vhdr = (struct vio_msg_hdr *)msg->rxbuf;
	if (!ret && vhdr) {
		struct eavb_stream_info *rsp = (struct eavb_stream_info *)(vhdr + 1);
		ret = vhdr->result;

		ASSERT(sizeof(struct eavb_stream_info) == sizeof(struct eavb_ioctl_stream_info));
		memcpy(&get_info.info, rsp, sizeof(struct eavb_stream_info));
	}

	virt_free_msg(priv, msg);
	copy_to_user(buf, &get_info, sizeof(get_info));

	return ret;
}

static int qavb_mmap(struct eavb_file *fl, void __user *buf)
{
	LOG_EAVB(MODULE_SETUP, LEVEL_INFO, "not support\n");
	return 0;
}
static int qavb_munmap(struct eavb_file *fl, void __user *buf)
{
	LOG_EAVB(MODULE_SETUP, LEVEL_INFO, "not support\n");
	return 0;
}

static int qavb_connect_stream(struct eavb_file *fl, void __user *buf)
{
	struct virtio_eavb_priv *priv = fl->priv;
	struct fe_msg *msg;
	struct vio_msg_hdr *vhdr;
	struct eavb_ioctl_connect_stream connect;
	int tsize, rsize;
	int index;
	int ret;

	copy_from_user(&connect, buf, sizeof(connect));
	LOG_EAVB(MODULE_SETUP, LEVEL_INFO, "streamCtx=0x%llx\n", connect.hdr.streamCtx);

	spin_lock(&fl->streamlock);
	index = getStreamIndex(fl, connect.hdr.streamCtx);
	spin_unlock(&fl->streamlock);
	if (index >= MAX_STREAM_NUM) {
		LOG_EAVB(MODULE_SETUP, LEVEL_ERR, "invalid streamCtx=0x%llx\n", connect.hdr.streamCtx);
		return -EINVAL;
	}

	tsize = rsize = sizeof(struct vio_connect_stream_msg);
	msg = virt_alloc_msg(priv, tsize, rsize);
	if (!msg) {
		LOG_EAVB(MODULE_SETUP, LEVEL_ERR, "stream%d alloc msg fail!\n", index);
		return -ENOMEM;
	}

	vhdr = (struct vio_msg_hdr *)msg->txbuf;
	vhdr->cmd = VIRTIO_EAVB_T_CONNECT_STREAM;
	vhdr->len = msg->txbuf_size;
	vhdr->streamctx_hdl = fl->stream[index].hdl;
	vhdr->stream_idx = fl->stream[index].idx;

	LOG_EAVB(MODULE_SETUP, LEVEL_DEBUG, "(%d:%d:%d)\n", fl->index, index, msg->msgid);
	LOG_EAVB(MODULE_SETUP, LEVEL_DEBUG, "stream%d (ctx 0x%llx, idx %d)\n", index, vhdr->streamctx_hdl, vhdr->stream_idx);
	ret = send_msg(priv, msg);
	LOG_EAVB(MODULE_SETUP, LEVEL_DEBUG, "(%d:%d:%d) ret=%d\n", fl->index, index, msg->msgid, ret);

	vhdr = (struct vio_msg_hdr *)msg->rxbuf;
	if (!ret && vhdr) {
		ret = vhdr->result;
		fl->stream[index].status = CONNECTED;
	}

	virt_free_msg(priv, msg);

	return ret;
}

static int qavb_disconnect_stream(struct eavb_file *fl, void __user *buf)
{
	struct virtio_eavb_priv *priv = fl->priv;
	struct fe_msg *msg;
	struct vio_msg_hdr *vhdr;
	struct eavb_ioctl_disconnect_stream disconnect;
	int tsize, rsize;
	int index;
	int ret;

	copy_from_user(&disconnect, buf, sizeof(disconnect));
	LOG_EAVB(MODULE_SETUP, LEVEL_INFO, "streamCtx=0x%llx\n", disconnect.hdr.streamCtx);

	spin_lock(&fl->streamlock);
	index = getStreamIndex(fl, disconnect.hdr.streamCtx);
	spin_unlock(&fl->streamlock);
	if (index >= MAX_STREAM_NUM) {
		LOG_EAVB(MODULE_SETUP, LEVEL_ERR, "invalid streamCtx=0x%llx\n", disconnect.hdr.streamCtx);
		return -EINVAL;
	}

	tsize = rsize = sizeof(struct vio_disconnect_stream_msg);
	msg = virt_alloc_msg(priv, tsize, rsize);
	if (!msg) {
		LOG_EAVB(MODULE_SETUP, LEVEL_ERR, "stream%d alloc msg fail!\n", index);
		return -ENOMEM;
	}

	vhdr = (struct vio_msg_hdr *)msg->txbuf;
	vhdr->cmd = VIRTIO_EAVB_T_DISCONNECT_STREAM;
	vhdr->len = msg->txbuf_size;
	vhdr->streamctx_hdl = fl->stream[index].hdl;
	vhdr->stream_idx = fl->stream[index].idx;

	LOG_EAVB(MODULE_SETUP, LEVEL_DEBUG, "(%d:%d:%d)\n", fl->index, index, msg->msgid);
	LOG_EAVB(MODULE_SETUP, LEVEL_DEBUG, "stream%d (ctx 0x%llx, idx %d)\n", index, vhdr->streamctx_hdl, vhdr->stream_idx);
	ret = send_msg(priv, msg);
	LOG_EAVB(MODULE_SETUP, LEVEL_DEBUG, "(%d:%d:%d) ret=%d\n", fl->index, index, msg->msgid, ret);

	vhdr = (struct vio_msg_hdr *)msg->rxbuf;
	if (!ret && vhdr) {
		ret = vhdr->result;
		fl->stream[index].status = DISCONNECTED;
	}

	virt_free_msg(priv, msg);

	return ret;
}

static int qavb_receive(struct eavb_file *fl, void __user *buf)
{
	struct virtio_eavb_priv *priv = fl->priv;
	struct fe_msg *msg;
	struct vio_msg_hdr *vhdr;
	struct vio_receive_msg *vmsg;
	struct eavb_ioctl_receive receive;
	int tsize, rsize;
	int mapping_index;
	int stream_index;
	int ret;

	copy_from_user(&receive, buf, sizeof(receive));
	mapping_index = get_mapping_idx(fl, receive.data.pbuf, receive.data.hdr.payload_size);
	if (mapping_index >= MAPPING_MAX) {
		LOG_EAVB(MODULE_RX, LEVEL_ERR, "invalid buf address 0x%llx\n", receive.data.pbuf);
		return -EINVAL;
	}

	spin_lock(&fl->streamlock);
	stream_index = getStreamIndex(fl, receive.hdr.streamCtx);
	spin_unlock(&fl->streamlock);
	if (stream_index >= MAX_STREAM_NUM) {
		LOG_EAVB(MODULE_SETUP, LEVEL_ERR, "invalid streamCtx=0x%llx\n", receive.hdr.streamCtx);
		return -EINVAL;
	}

	tsize = rsize = sizeof(struct vio_receive_msg);
	msg = virt_alloc_msg(priv, tsize, rsize);
	if (!msg) {
		LOG_EAVB(MODULE_SETUP, LEVEL_ERR, "stream%d alloc msg fail!\n", stream_index);
		return -ENOMEM;
	}

	vhdr = (struct vio_msg_hdr *)msg->txbuf;
	vhdr->cmd = VIRTIO_EAVB_T_RECEIVE;
	vhdr->len = msg->txbuf_size;
	vhdr->streamctx_hdl = fl->stream[stream_index].hdl;
	vhdr->stream_idx = fl->stream[stream_index].idx;

	vmsg = (struct vio_receive_msg *)vhdr;
	ASSERT(sizeof(struct eavb_buf_data) == sizeof(struct eavb_ioctl_buf_data));
	memcpy(&vmsg->data, &receive.data, sizeof(struct eavb_buf_data));

	vmsg->data.gpa = get_mapping_phyaddr(fl, receive.data.pbuf, mapping_index);
	LOG_EAVB(MODULE_RX, LEVEL_DEBUG, "pass phy address 0x%llx\n", vmsg->data.gpa);
	LOG_EAVB(MODULE_RX, LEVEL_DEBUG, "stream%d (ctx 0x%llx, idx %d)\n", stream_index, vhdr->streamctx_hdl, vhdr->stream_idx);

	ret = send_msg(priv, msg);

	vhdr = (struct vio_msg_hdr *)msg->rxbuf;
	if (!ret && vhdr) {
		ret = vhdr->result;
		vmsg = (struct vio_receive_msg *)vhdr;
		receive.received = vmsg->received;
		memcpy(&receive.data, &vmsg->data, sizeof(struct eavb_buf_data));
		if (receive.received) {
			eavb_place_marker_once("M - DRIVER EAVB FE First received data");
		}
	}

	virt_free_msg(priv, msg);

	copy_to_user(buf, &receive, sizeof(receive));
	return ret;
}

static int qavb_recv_done(struct eavb_file *fl, void __user *buf)
{
	struct eavb_ioctl_recv_done done;
	copy_from_user(&done, buf, sizeof(done));

	return 0;
}

static int qavb_transmit(struct eavb_file *fl, void __user *buf)
{
	struct virtio_eavb_priv *priv = fl->priv;
	struct fe_msg *msg;
	struct vio_msg_hdr *vhdr;
	struct vio_transmit_msg *vmsg;
	struct eavb_ioctl_transmit transmit;
	int tsize, rsize;
	int mapping_index;
	int stream_index;
	int ret;

	copy_from_user(&transmit, buf, sizeof(transmit));
	mapping_index = get_mapping_idx(fl, transmit.data.pbuf, transmit.data.hdr.payload_size);
	if (mapping_index >= MAPPING_MAX) {
		LOG_EAVB(MODULE_RX, LEVEL_ERR, "invalid buf address 0x%llx\n", transmit.data.pbuf);
		return -EINVAL;
	}

	spin_lock(&fl->streamlock);
	stream_index = getStreamIndex(fl, transmit.hdr.streamCtx);
	spin_unlock(&fl->streamlock);
	if (stream_index >= MAX_STREAM_NUM) {
		LOG_EAVB(MODULE_SETUP, LEVEL_ERR, "invalid streamCtx=0x%llx\n", transmit.hdr.streamCtx);
		return -EINVAL;
	}

	tsize = rsize = sizeof(struct vio_transmit_msg);
	msg = virt_alloc_msg(priv, tsize, rsize);
	if (!msg) {
		LOG_EAVB(MODULE_SETUP, LEVEL_ERR, "stream%d alloc msg fail!\n", stream_index);
		return -ENOMEM;
	}

	vhdr = (struct vio_msg_hdr *)msg->txbuf;
	vhdr->cmd = VIRTIO_EAVB_T_TRANSMIT;
	vhdr->len = msg->txbuf_size;
	vhdr->streamctx_hdl = fl->stream[stream_index].hdl;
	vhdr->stream_idx = fl->stream[stream_index].idx;

	vmsg = (struct vio_transmit_msg *)vhdr;
	ASSERT(sizeof(struct eavb_buf_data) == sizeof(struct eavb_ioctl_buf_data));
	memcpy(&vmsg->data, &transmit.data, sizeof(struct eavb_buf_data));

	vmsg->data.gpa = get_mapping_phyaddr(fl, transmit.data.pbuf, mapping_index);
	LOG_EAVB(MODULE_TX, LEVEL_DEBUG, "pass phy address 0x%llx\n", vmsg->data.gpa);
	LOG_EAVB(MODULE_TX, LEVEL_DEBUG, "stream%d (ctx 0x%llx, idx %d)\n", stream_index, vhdr->streamctx_hdl, vhdr->stream_idx);

	ret = send_msg(priv, msg);

	vhdr = (struct vio_msg_hdr *)msg->rxbuf;
	if (!ret && vhdr) {
		ret = vhdr->result;
		vmsg = (struct vio_transmit_msg *)vhdr;
		transmit.written = vmsg->written;
	}

	virt_free_msg(priv, msg);

	copy_to_user(buf, &transmit, sizeof(transmit));
	return ret;
}

static long virtio_eavb_ioctl(struct file *file, unsigned int ioctl_cmd,
                                 unsigned long ioctl_param)
{
	struct eavb_file *fl = file->private_data;
	struct virtio_eavb_priv *priv = fl->priv;
	int ret = 0;

	//LOG_EAVB(MODULE_SETUP, LEVEL_DEBUG, "fl->index=%d, ioctl_cmd = 0x%x\n", fl->index, ioctl_cmd);

	switch (ioctl_cmd) {
		// pass the qavb config from FE to BE
		case EAVB_IOCTL_CREATE_STREAM:
			ret = qavb_create_stream(fl, (void __user*)ioctl_param);
			break;
		case EAVB_IOCTL_CREATE_STREAM_WITH_PATH:
			ret = qavb_create_stream_with_path(fl, (void __user*)ioctl_param);
			break;
		case EAVB_IOCTL_GET_STREAM_INFO:
			ret = qavb_get_stream_info(fl, (void __user*)ioctl_param);
			break;
		case EAVB_IOCTL_CONNECT_STREAM:
			ret = qavb_connect_stream(fl, (void __user*)ioctl_param);
			break;
			// read()
		case EAVB_IOCTL_RECEIVE:
			ret = qavb_receive(fl, (void __user*)ioctl_param);
			break;
		case EAVB_IOCTL_RECV_DONE:
			ret = qavb_recv_done(fl, (void __user*)ioctl_param);
			break;
			// write()
		case EAVB_IOCTL_TRANSMIT:
			ret = qavb_transmit(fl, (void __user*)ioctl_param);
			break;
			// create a ring buffer by using ION
		case EAVB_IOCTL_MMAP:       // allocate ion buffer
			ret = qavb_mmap(fl, (void __user*)ioctl_param);
			break;
		case EAVB_IOCTL_MUNMAP:     // free ion buffer
			ret = qavb_munmap(fl, (void __user*)ioctl_param);
			break;
		case EAVB_IOCTL_DISCONNECT_STREAM:
			ret = qavb_disconnect_stream(fl, (void __user*)ioctl_param);
			break;
		case EAVB_IOCTL_DESTROY_STREAM:
			ret = qavb_destroy_stream(fl, (void __user*)ioctl_param);
			break;
			// TODO: invoke qavb_register_stream_event_cb() for CRF stream
		default:
			ret = -ENOTTY;
			LOG_EAVB(MODULE_SETUP, LEVEL_ERR, "unsupported ioctl 0x%x\n", ioctl_cmd);

	}

	return ret;
}

static long virtio_eavb_compat_ioctl(struct file* file,
				unsigned int ioctl_cmd,
				unsigned long ioctl_param)
{
	return -ENOTSUPP;
}

static int virtio_eavb_mmap(struct file* file, struct vm_area_struct *vma)
{
	struct eavb_file *fl = file->private_data;
	//struct virtio_eavb_priv *priv = fl->priv;
	int ret = 0;
	unsigned long vsize;
	u8 *buf;

	spin_lock(&fl->mappinglock);
	if (fl->mapping_count < MAPPING_MAX) {
		fl->mapping_count ++;
	} else {
		ret = -EBUSY;
	}
	spin_unlock(&fl->mappinglock);
	if (ret) {
		LOG_EAVB(MODULE_SETUP, LEVEL_ERR, "Too many mapping (%d):fl->index %d\n", fl->mapping_count, fl->index);
		return ret;
	}

	vsize = vma->vm_end - vma->vm_start;
	LOG_EAVB(MODULE_SETUP, LEVEL_DEBUG, "vsize = %ld(0x%lx~0x%lx)\n", vsize, vma->vm_start, vma->vm_end);
	buf = kmalloc(vsize, GFP_KERNEL);
	vma->vm_flags |= VM_IO;
	vma->vm_flags |= VM_LOCKED;

	if (remap_pfn_range(vma, vma->vm_start, virt_to_phys(buf)>>PAGE_SHIFT, vsize, vma->vm_page_prot)) {
		LOG_EAVB(MODULE_SETUP, LEVEL_ERR, "remap pfn range failed\n");
		spin_lock(&fl->mappinglock);
		fl->mapping_count --;
		spin_unlock(&fl->mappinglock);
		ret = -EAGAIN;
	}
	memset(buf, 0, vsize);

	add_mapping_info(fl, (uint64_t)buf, vma->vm_start, virt_to_phys(buf), vsize);

	return ret;
}

static const struct file_operations fops = {
	.open = virtio_eavb_open,
	.release = virtio_eavb_release,
	.unlocked_ioctl = virtio_eavb_ioctl,
	.compat_ioctl = virtio_eavb_compat_ioctl,
	.mmap = virtio_eavb_mmap,
};

static void fe_recv_done(struct virtqueue *rvq)
{
	struct virtio_eavb_priv *priv = rvq->vdev->priv;
	struct vio_msg_hdr *rsp;
	struct fe_msg *msg;
	unsigned int len;
	unsigned long flags;


	//LOG_EAVB(MODULE_RX, LEVEL_DEBUG, "\n");

	while(1) {
		spin_lock_irqsave(&priv->rvqlock, flags);
		rsp = virtqueue_get_buf(rvq, &len);
		spin_unlock_irqrestore(&priv->rvqlock, flags);

		if (!rsp) break;

		ASSERT(len == RX_BUF_MAX_LEN);
		if (len < rsp->len) {
			LOG_EAVB(MODULE_RX, LEVEL_ERR, "rsp msgid %d len %d mismatch received len %d!\n", rsp->msgid, rsp->len, len);
			continue;
		}

		if (rsp->msgid > FE_MSG_MAX - 1) {
			LOG_EAVB(MODULE_RX, LEVEL_ERR, "rsp msgid %d is invalid!\n", rsp->msgid);
			continue;
		}

		msg = priv->msgtable[rsp->msgid];
		if (!msg) {
			LOG_EAVB(MODULE_RX, LEVEL_ERR, "rsp msgid %d found no msg!\n", rsp->msgid);
			continue;
		}
		if (msg->msgid != rsp->msgid) {
			LOG_EAVB(MODULE_RX, LEVEL_ERR, "rsp msgid %d mismatch msg msgid %d!\n", rsp->msgid, msg->msgid);
			continue;
		}

		msg->rxbuf = (void *)rsp;
		msg->rxbuf_used = 1;
		LOG_EAVB(MODULE_RX, LEVEL_DEBUG, "msgid %d rxbuf used\n", msg->msgid);
		complete(&msg->work);
	}
}

static int init_vqs(struct virtio_eavb_priv *priv)
{
	struct virtqueue *vqs[2];
	static const char * const names[] = { "eavb_tx", "eavb_rx" };
	vq_callback_t *cbs[] = {NULL, fe_recv_done};

	int ret;

	ret = virtio_find_vqs(priv->vdev, 2, vqs, cbs, names, NULL);
	if (ret) {
		LOG_EAVB(MODULE_SETUP, LEVEL_ERR, "virtio_find_vqs fail \n");
		return ret;
	}

	priv->svq = vqs[0];
	priv->rvq = vqs[1];

	return 0;
}

static int virtio_eavb_probe(struct virtio_device *vdev)
{
	int ret;
	struct device *dev = NULL;
	struct virtio_eavb_priv *priv;
	int i;

	LOG_EAVB(MODULE_SETUP, LEVEL_DEBUG, "\n");
	priv = kmalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		ret = -ENOMEM;
		goto fail;
	}
	memset(priv, 0, sizeof(*priv));

	if (virtio_has_feature(vdev, VIRTIO_EAVB_F_SHMEM))
		priv->has_shmem = true;

	mutex_init(&priv->lock);
	spin_lock_init(&priv->msglock);
	spin_lock_init(&priv->rvqlock);

	vdev->priv = priv;
	priv->vdev = vdev;
	priv->dev = vdev->dev.parent;
	priv->name = "eavb";

	ret = init_vqs(priv);
	if (ret) {
		goto free_priv;
	}
	virtio_device_ready(vdev);

	ret = alloc_chrdev_region(&priv->dev_no, 0, DEVICE_NUM, DEVICE_NAME);
	if (ret)
		goto free_priv;

	cdev_init(&priv->cdev, &fops);
	priv->cdev.owner = THIS_MODULE;
	ret = cdev_add(&priv->cdev, MKDEV(MAJOR(priv->dev_no), 0), DEVICE_NUM);
	if (ret)
		goto free_chrdev;

	priv->class = class_create(THIS_MODULE, "virt-eavb");
	if (IS_ERR(priv->class))
		goto class_create_fail;

	dev = device_create(priv->class, NULL,
			MKDEV(MAJOR(priv->dev_no), MINOR_NUM_DEV),
			NULL, DEVICE_NAME);
	if (IS_ERR_OR_NULL(dev))
		goto device_create_fail;

	priv->rxbufs[0] = kmalloc(FE_MSG_MAX * RX_BUF_MAX_LEN, GFP_KERNEL);
	if (!priv->rxbufs[0]) {
		ret = -ENOMEM;
		goto alloc_rxbufs_fail;
	}

	for (i = 0; i<FE_MSG_MAX; i++) {
		struct scatterlist sg;
		u8 *rsp;

		rsp = priv->rxbufs[0] + i * RX_BUF_MAX_LEN;
		priv->rxbufs[i] = rsp;

		sg_init_one(&sg, rsp, RX_BUF_MAX_LEN);
		ret = virtqueue_add_inbuf(priv->rvq, &sg, 1, rsp, GFP_KERNEL);
		WARN_ON(ret);
	}
	virtqueue_disable_cb(priv->svq);

	virtqueue_enable_cb(priv->rvq);
	virtqueue_kick(priv->rvq);

	place_marker("M - DRIVER EAVB FE Ready");
	return 0;

alloc_rxbufs_fail:
	device_destroy(priv->class, MKDEV(MAJOR(priv->dev_no), MINOR_NUM_DEV));
device_create_fail:
	if (!IS_ERR_OR_NULL(dev))
		device_destroy(priv->class, MKDEV(MAJOR(priv->dev_no),
				MINOR_NUM_DEV));
	class_destroy(priv->class);
class_create_fail:
	cdev_del(&priv->cdev);
free_chrdev:
	unregister_chrdev_region(priv->dev_no, DEVICE_NUM);
free_priv:
	kfree(priv);
fail:
	return ret;
}

static void virtio_eavb_remove(struct virtio_device *vdev)
{
	struct virtio_eavb_priv *priv;

	LOG_EAVB(MODULE_SETUP, LEVEL_DEBUG, "\n");
	priv = vdev->priv;

	device_destroy(priv->class, MKDEV(MAJOR(priv->dev_no), MINOR_NUM_DEV));
	class_destroy(priv->class);
	cdev_del(&priv->cdev);
	unregister_chrdev_region(priv->dev_no, DEVICE_NUM);

	vdev->config->reset(vdev);
	vdev->config->del_vqs(vdev);
	kfree(priv->rxbufs[0]);
	kfree(priv);
}


const static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_EAVB, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static unsigned int features[] = {
	VIRTIO_EAVB_F_SHMEM,
};

static struct virtio_driver virtio_eavb_driver = {
	.feature_table = features,
	.feature_table_size = ARRAY_SIZE(features),
	.driver.name = KBUILD_MODNAME,
	.driver.owner = THIS_MODULE,
	.id_table = id_table,
	.probe = virtio_eavb_probe,
	.remove = virtio_eavb_remove,
};

static int __init virtio_eavb_init(void)
{
	place_marker("M - DRIVER EAVB FE Init");
	return register_virtio_driver(&virtio_eavb_driver);
}

static void __exit virtio_eavb_exit(void)
{
	unregister_virtio_driver(&virtio_eavb_driver);
}

module_init(virtio_eavb_init);
module_exit(virtio_eavb_exit);

MODULE_LICENSE("GPL v2");
