/* Copyright (c) 2017, The Linux Foundation. All rights reserved.
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

/*!@file: ptp_virtual.c
 * @brief: Driver functions.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/net.h>
#include <linux/ptp_clock_kernel.h>

#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/uio.h>

#include <linux/netpoll.h>
#include <linux/inet.h>

#include <linux/errno.h>
#include <linux/types.h>

#include <linux/netdevice.h>
#include <linux/ip.h>
#include <linux/in.h>

#include <linux/un.h>
#include <linux/ctype.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <net/inet_connection_sock.h>
#include <net/request_sock.h>
#include <linux/io.h>
#include <linux/time.h>

//#define DEBUG_ENABLED
//#define PTP_READ_WTIH_SOCKET
//#define MEASURE_ROUNDTRIP_TIME
#define PTP_MV_SYSCLOCK    125000000

#ifdef DEBUG_ENABLED
#define PTP_VIRTUAL_DEBUG_L1(...) printk(KERN_ALERT __VA_ARGS__)
#else
#define PTP_VIRTUAL_DEBUG_L1(...)
#endif
#define PTP_VIRTUAL_ALERT(...) printk(KERN_ERR __VA_ARGS__)

#ifdef PTP_READ_WTIH_SOCKET
#define PTP_VM_get_time PTP_VM_get_time_with_socket
#else
#define PTP_VM_get_time PTP_VM_get_time_with_vdev
#endif

struct ptp_clock * ptp_vm_clk;

#ifdef PTP_READ_WTIH_SOCKET
#define GET_PTP_TS 101
static struct socket *cscok;

static int ptp_virtual_setup_socket(void)
{
	int ret = 0;
	int error = 0;
	struct sockaddr_in sin;
	int port = 8080;
	cscok = NULL;

	PTP_VIRTUAL_DEBUG_L1("Entry: %s.\n", __func__);
	cscok=(struct socket*)kmalloc(sizeof(struct socket),GFP_KERNEL);
	error = sock_create(PF_INET,SOCK_STREAM,IPPROTO_TCP,&cscok);

	if(error<0) {
		PTP_VIRTUAL_ALERT("%s: CREATE CSOCKET ERROR\n", __func__);
		goto err;
	}

	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = in_aton("192.168.0.1");
	sin.sin_port = htons(port);

	error = cscok->ops->connect(cscok, (struct sockaddr*) &sin, sizeof(sin), 0);

	if(error<0) {
		PTP_VIRTUAL_ALERT("%s: CONNECT CSOCKET ERROR\n", __func__);
	}
	PTP_VIRTUAL_DEBUG_L1("Exit: %s.\n", __func__);
	return ret;
err:
	kfree(cscok);
	cscok = NULL;
	PTP_VIRTUAL_ALERT("%s: cscok = NULL\n", __func__);
	return ret;
}

static void ptp_virtual_disconnect_socket(void)
{
	PTP_VIRTUAL_DEBUG_L1("Entry: %s.\n", __func__);
	if(cscok) {
		cscok->ops->shutdown(cscok, SHUT_RDWR);
		kfree(cscok);
		cscok = NULL;
	}
	PTP_VIRTUAL_DEBUG_L1("Exit: %s.\n", __func__);
}

static int PTP_VM_get_time_with_socket(struct ptp_clock_info *ptp, struct timespec *ts)
{
	int error, request;
	uint32_t hw_ts[2];
	struct msghdr msg;
	struct iovec iov;
	mm_segment_t oldfs;
	int size=0;
#ifdef MEASURE_ROUNDTRIP_TIME
	struct timespec before;
	struct timespec after;
	unsigned long long before_ns;
	unsigned long long after_ns;
#endif
	PTP_VIRTUAL_DEBUG_L1("Entry: %s.\n", __func__);
	hw_ts[0] = 0;
	hw_ts[1] = 0;
	ts->tv_sec = 0;
	ts->tv_nsec = 0;
	if(cscok == NULL) {
		PTP_VIRTUAL_ALERT("%s: No connection with the host\n", __func__);
		return -1;
	}

	request = GET_PTP_TS;
	iov.iov_base=&request;
	iov.iov_len=sizeof(request);

	msg.msg_control=NULL;
	msg.msg_controllen=0;
	msg.msg_flags=0;
	msg.msg_iov=&iov;
	msg.msg_iovlen=1;
	msg.msg_name=0;
	msg.msg_namelen=0;

	oldfs=get_fs();
	set_fs(KERNEL_DS);
#ifdef MEASURE_ROUNDTRIP_TIME
	getnstimeofday(&before);
#endif
	size=sock_sendmsg(cscok,&msg,sizeof(request));

	if(error<0) {
		PTP_VIRTUAL_ALERT("%s: SEND MSG ERROR\n", __func__);
		goto err;
	}

	set_fs(oldfs);
	iov.iov_base=hw_ts;
	iov.iov_len=sizeof(hw_ts);

	msg.msg_control=NULL;
	msg.msg_controllen=0;
	msg.msg_flags=0;
	msg.msg_name=0;
	msg.msg_namelen=0;
	msg.msg_iov=&iov;
	msg.msg_iovlen=1;

	oldfs=get_fs();
	set_fs(KERNEL_DS);
	size=sock_recvmsg(cscok,&msg,sizeof(hw_ts),msg.msg_flags);
#ifdef MEASURE_ROUNDTRIP_TIME
	getnstimeofday(&after);
	before_ns = before.tv_sec*1000000000LL + before.tv_nsec;
	after_ns = after.tv_sec*1000000000LL + after.tv_nsec;
	PTP_VIRTUAL_DEBUG_L1("%s: Socket read finished in (%lld)nano sec\n", __func__, (after_ns - before_ns));
#endif
	if(error<0) {
		PTP_VIRTUAL_ALERT("%s: REC MSG ERROR\n", __func__);
		goto err;
	}
	set_fs(oldfs);

	ts->tv_sec = hw_ts[0];
	ts->tv_nsec = hw_ts[1];
	PTP_VIRTUAL_DEBUG_L1("Exit: %s.\n", __func__);
	return 0;
err:
	cscok->ops->shutdown(cscok, SHUT_RDWR);
	kfree(cscok);
	cscok = NULL;
	return error;
}
#else
static void __iomem *virtbase;

static int PTP_VM_get_time_with_vdev(struct ptp_clock_info *ptp, struct timespec *ts)
{
	uint32_t hw_ts[2];
#ifdef MEASURE_ROUNDTRIP_TIME
	struct timespec before;
	struct timespec after;
	unsigned long long before_ns;
	unsigned long long after_ns;
#endif
	hw_ts[0] = 0;
	hw_ts[1] = 0;
	ts->tv_sec = 0;
	ts->tv_nsec = 0;
	PTP_VIRTUAL_DEBUG_L1("Entry: %s.\n", __func__);
	if(virtbase == NULL) {
		PTP_VIRTUAL_ALERT("%s: memory not mapped for vdev\n", __func__);
		return 0;
	}
#ifdef MEASURE_ROUNDTRIP_TIME
	getnstimeofday(&before);
#endif
	memcpy_fromio(hw_ts, virtbase, sizeof(hw_ts));

#ifdef MEASURE_ROUNDTRIP_TIME
	getnstimeofday(&after);
	before_ns = before.tv_sec*1000000000LL + before.tv_nsec;
	after_ns = after.tv_sec*1000000000LL + after.tv_nsec;
	PTP_VIRTUAL_DEBUG_L1("%s: vdev read finished in (%lld)nano sec\n", __func__, (after_ns - before_ns));
#endif
	ts->tv_sec = hw_ts[0];
	ts->tv_nsec = hw_ts[1];
	PTP_VIRTUAL_DEBUG_L1("Exit: %s.\n", __func__);
	return 0;
}

static int ptp_virtual_setup_vdev(struct device *dev_node)
{
	int32_t ret = 0;
	uint32_t dev_addr = 0;

	PTP_VIRTUAL_DEBUG_L1("Entry: %s.\n", __func__);
	ret = of_property_read_u32(dev_node, "qcom,ptp-vdev-addr", &dev_addr);
	PTP_VIRTUAL_ALERT("%s: read vdev-addr: %0x\n", __func__, dev_addr);
	virtbase = ioremap(dev_addr, 2048);
	PTP_VIRTUAL_ALERT("%s: virtbase: %0x\n", __func__, virtbase);
	PTP_VIRTUAL_DEBUG_L1("Exit: %s.\n", __func__);
	return 0;
}

static void ptp_virtual_disconnect_vdev(void)
{
	PTP_VIRTUAL_DEBUG_L1("Entry: %s.\n", __func__);
	iounmap(virtbase);
	virtbase = NULL;
	PTP_VIRTUAL_DEBUG_L1("Exit: %s.\n", __func__);
	PTP_VIRTUAL_DEBUG_L1("Exit: %s.\n", __func__);
}

#endif

static int PTP_VM_adjust_freq(struct ptp_clock_info *ptp, s32 ppb)
{
	PTP_VIRTUAL_DEBUG_L1("Entry: %s.\n", __func__);
	PTP_VIRTUAL_DEBUG_L1("Exit: %s.\n", __func__);
	return 0;
}

static int PTP_VM_adjust_time(struct ptp_clock_info *ptp, s64 delta)
{
	PTP_VIRTUAL_DEBUG_L1("Entry: %s.\n", __func__);
	PTP_VIRTUAL_DEBUG_L1("Exit: %s.\n", __func__);
	return 0;
}


static int PTP_VM_set_time(struct ptp_clock_info *ptp, const struct timespec *ts)
{
	PTP_VIRTUAL_DEBUG_L1("Entry: %s.\n", __func__);
	PTP_VIRTUAL_DEBUG_L1("Exit: %s.\n", __func__);
	return 0;
}

static int PTP_VM_enable(struct ptp_clock_info *ptp,
        struct ptp_clock_request *rq, int on)
{
        return -EOPNOTSUPP;
}

static struct ptp_clock_info PTP_VM_ptp_clock_ops = {
	.owner = THIS_MODULE,
	.name = "ptp_virtual",
	.max_adj = PTP_MV_SYSCLOCK, /* the max possible frequency adjustment,
				in parts per billion */
	.n_alarm = 0,	/* the number of programmable alarms */
	.n_ext_ts = 0,	/* the number of externel time stamp channels */
	.n_per_out = 0, /* the number of programmable periodic signals */
	.pps = 0,	/* indicates whether the clk supports a PPS callback */
	.adjfreq = PTP_VM_adjust_freq,
	.adjtime = PTP_VM_adjust_time,
#if ( (LINUX_VERSION_CODE) > (KERNEL_VERSION(4,1,0)) )
	.gettime64 = PTP_VM_get_time,
	.settime64 = PTP_VM_set_time,
#else
	.gettime = PTP_VM_get_time,
	.settime = PTP_VM_set_time,
#endif
	.enable = PTP_VM_enable,
};


static int ptp_virtual_probe(struct platform_device *pdev)
{
	int ret = 0;

	PTP_VIRTUAL_DEBUG_L1("Entry: %s\n", __func__);
	ptp_vm_clk = ptp_clock_register(&PTP_VM_ptp_clock_ops, NULL);
	if (IS_ERR(ptp_vm_clk)) {
		PTP_VIRTUAL_ALERT("%s: ptp clock registratin failed\n", __func__);
		ptp_vm_clk = NULL;
	} else {
		PTP_VIRTUAL_ALERT("%s: ptp clock registered:\n", __func__);
	}
#ifdef PTP_READ_WTIH_SOCKET
	ret = ptp_virtual_setup_socket();
#else
	ret = ptp_virtual_setup_vdev(pdev->dev.of_node);
#endif
	PTP_VIRTUAL_DEBUG_L1("Exit: %s\n", __func__);
	return ret;
}

static int ptp_virtual_remove(struct platform_device *pdev)
{
	int ret = 0;

	PTP_VIRTUAL_DEBUG_L1("Entry:%s.\n", __func__);
	ptp_clock_unregister(ptp_vm_clk);
	ptp_vm_clk = NULL;
	PTP_VIRTUAL_DEBUG_L1("Exit:%s.\n", __func__);
	return ret;
}

static struct of_device_id ptp_virtual_match[] = {
	{	.compatible = "qcom,ptp_virtual",
	},
	{}
};

static struct platform_driver ptp_virtual_driver = {
	.probe	= ptp_virtual_probe,
	.remove	= ptp_virtual_remove,
	.driver	= {
		.name		= "ptp_virtual",
		.owner		= THIS_MODULE,
		.of_match_table	= ptp_virtual_match,
	},
};

static int __init ptp_virtual_init(void)
{
	int ret = 0;

	PTP_VIRTUAL_DEBUG_L1("Entry:%s.\n", __func__);
	ret = platform_driver_register(&ptp_virtual_driver);
	PTP_VIRTUAL_DEBUG_L1("Exit:%s.\n", __func__);
	return ret;
}

static void __exit ptp_virtual_exit(void)
{
	PTP_VIRTUAL_DEBUG_L1("Entry:%s.\n", __func__);
#ifdef PTP_READ_WTIH_SOCKET
	ptp_virtual_disconnect_socket();
#else
	ptp_virtual_disconnect_vdev();
#endif
	platform_driver_unregister(&ptp_virtual_driver);
	PTP_VIRTUAL_DEBUG_L1("Exit:%s.\n", __func__);
}

module_init(ptp_virtual_init);
module_exit(ptp_virtual_exit);
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, ptp_virtual_match);
