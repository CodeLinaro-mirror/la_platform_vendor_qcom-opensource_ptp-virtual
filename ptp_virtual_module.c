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
#include <linux/of.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/uio.h>

#include <linux/netpoll.h>
#include <linux/inet.h>

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/netdevice.h>
#include <linux/ip.h>
#include <linux/slab.h>
#include <linux/in.h>

#include <linux/un.h>
#include <linux/ctype.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <net/inet_connection_sock.h>
#include <net/request_sock.h>
#include <linux/io.h>
#include <linux/time.h>
#include <linux/math64.h>

#define QTIMER_PASS_THRU
#define GPTP_GVM_SHM_MODE
#define DWC_QOS_ETH_HANA
//#define DEBUG_ENABLED
//#define PTP_READ_WTIH_SOCKET
//#define MEASURE_ROUNDTRIP_TIME
#define PTP_READ_WTIH_PASS_THRU
#define PTP_MV_SYSCLOCK    125000000

#ifdef DEBUG_ENABLED
#define PTP_VIRTUAL_DEBUG_L1(...) printk(KERN_ALERT __VA_ARGS__)
#else
#define PTP_VIRTUAL_DEBUG_L1(...)
#endif
#define PTP_VIRTUAL_ALERT(...) printk(KERN_ERR __VA_ARGS__)

#ifdef PTP_READ_WTIH_SOCKET
#define PTP_VM_get_time PTP_VM_get_time_with_socket
#endif

#ifdef PTP_READ_WTIH_VDEV
#define PTP_VM_get_time PTP_VM_get_time_with_vdev
#endif

#ifdef PTP_READ_WTIH_PASS_THRU
#define PTP_VM_get_time PTP_VM_get_time_with_pass_through
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
    cscok = (struct socket*)kmalloc(sizeof(struct socket), GFP_KERNEL);
    error = sock_create(PF_INET, SOCK_STREAM, IPPROTO_TCP, &cscok);

    if (error < 0) {
        PTP_VIRTUAL_ALERT("%s: CREATE CSOCKET ERROR\n", __func__);
        goto err;
    }

    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = in_aton("192.168.0.1");
    sin.sin_port = htons(port);
    error = cscok->ops->connect(cscok, (struct sockaddr*) &sin, sizeof(sin), 0);

    if (error < 0) {
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

    if (cscok) {
        cscok->ops->shutdown(cscok, SHUT_RDWR);
        kfree(cscok);
        cscok = NULL;
    }

    PTP_VIRTUAL_DEBUG_L1("Exit: %s.\n", __func__);
}

static int PTP_VM_get_time_with_socket(struct ptp_clock_info *ptp,
                                       struct timespec64 *ts)
{
    int error, request;
    uint32_t hw_ts[2];
    struct msghdr msg;
    struct iovec iov;
    mm_segment_t oldfs;
    int size = 0;
#ifdef MEASURE_ROUNDTRIP_TIME
    struct timespec64 before;
    struct timespec64 after;
    unsigned long long before_ns;
    unsigned long long after_ns;
#endif
    PTP_VIRTUAL_DEBUG_L1("Entry: %s.\n", __func__);
    hw_ts[0] = 0;
    hw_ts[1] = 0;
    ts->tv_sec = 0;
    ts->tv_nsec = 0;

    if (cscok == NULL) {
        PTP_VIRTUAL_ALERT("%s: No connection with the host\n", __func__);
        return -1;
    }

    request = GET_PTP_TS;
    iov.iov_base = &request;
    iov.iov_len = sizeof(request);
    msg.msg_control = NULL;
    msg.msg_controllen = 0;
    msg.msg_flags = 0;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_name = 0;
    msg.msg_namelen = 0;
    oldfs = get_fs();
    set_fs(KERNEL_DS);
#ifdef MEASURE_ROUNDTRIP_TIME
    getnstimeofday(&before);
#endif
    size = sock_sendmsg(cscok, &msg, sizeof(request));

    if (error < 0) {
        PTP_VIRTUAL_ALERT("%s: SEND MSG ERROR\n", __func__);
        goto err;
    }

    set_fs(oldfs);
    iov.iov_base = hw_ts;
    iov.iov_len = sizeof(hw_ts);
    msg.msg_control = NULL;
    msg.msg_controllen = 0;
    msg.msg_flags = 0;
    msg.msg_name = 0;
    msg.msg_namelen = 0;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    oldfs = get_fs();
    set_fs(KERNEL_DS);
    size = sock_recvmsg(cscok, &msg, sizeof(hw_ts), msg.msg_flags);
#ifdef MEASURE_ROUNDTRIP_TIME
    getnstimeofday(&after);
    before_ns = before.tv_sec * 1000000000LL + before.tv_nsec;
    after_ns = after.tv_sec * 1000000000LL + after.tv_nsec;
    PTP_VIRTUAL_DEBUG_L1("%s: Socket read finished in (%lld)nano sec\n", __func__,
                         (after_ns - before_ns));
#endif

    if (error < 0) {
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
#endif

#ifdef PTP_READ_WTIH_VDEV
static void __iomem *virtbase;

static int PTP_VM_get_time_with_vdev(struct ptp_clock_info *ptp,
                                     struct timespec64 *ts)
{
    uint32_t hw_ts[2];
#ifdef MEASURE_ROUNDTRIP_TIME
    struct timespec64 before;
    struct timespec64 after;
    unsigned long long before_ns;
    unsigned long long after_ns;
#endif
    hw_ts[0] = 0;
    hw_ts[1] = 0;
    ts->tv_sec = 0;
    ts->tv_nsec = 0;
    PTP_VIRTUAL_DEBUG_L1("Entry: %s.\n", __func__);

    if (virtbase == NULL) {
        PTP_VIRTUAL_ALERT("%s: memory not mapped for vdev\n", __func__);
        return 0;
    }

#ifdef MEASURE_ROUNDTRIP_TIME
    getnstimeofday(&before);
#endif
    memcpy_fromio(hw_ts, virtbase, sizeof(hw_ts));
#ifdef MEASURE_ROUNDTRIP_TIME
    getnstimeofday(&after);
    before_ns = before.tv_sec * 1000000000LL + before.tv_nsec;
    after_ns = after.tv_sec * 1000000000LL + after.tv_nsec;
    PTP_VIRTUAL_DEBUG_L1("%s: vdev read finished in (%lld)nano sec\n", __func__,
                         (after_ns - before_ns));
#endif
    ts->tv_sec = hw_ts[0];
    ts->tv_nsec = hw_ts[1];
    PTP_VIRTUAL_DEBUG_L1("Exit: %s.\n", __func__);
    return 0;
}

static int ptp_virtual_setup_vdev(struct device_node *dev_node)
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

#ifdef PTP_READ_WTIH_PASS_THRU
static void __iomem *virtbase;
//#ifdef QTIMER_PASS_THRU
#define QTMR0_F0V2_QTMR_V2_ADDR 0x17C23000
#define QTMR0_F0V2_QTMR_V2_SIZE 0x1000
static void __iomem *qtimer_virtbase;
static void __iomem *gptp_virtbase;
//#endif

static resource_size_t ntnbase = 0;
static resource_size_t qtmraddr = 0;
static resource_size_t ntnbase_size = 0;
static resource_size_t qtmraddr_size = 0;

#define NTN_BASE_ADDRESS 0xd60a000

/*Support more GMAC such as HANA EMAC*/
#ifdef DWC_QOS_ETH_HANA
#undef NTN_BASE_ADDRESS
#define NTN_BASE_ADDRESS 0x20000

static int ptp_virtual_setup_passthrough(struct platform_device *pdev)
{
    int32_t ret = 0;
    struct resource *res;
    PTP_VIRTUAL_DEBUG_L1("Entry: %s.\n", __func__);
    res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ptp_reg");

    if (res) {
        ntnbase = res->start;
        ntnbase_size = resource_size(res);
        PTP_VIRTUAL_ALERT("%s: read ntnbase: 0x%0llx size: 0x%llx\n",
                          __func__, ntnbase, ntnbase_size);
    } else {
        PTP_VIRTUAL_ALERT("%s: get ptp_virtual ntnbase resource failed\n", __func__);
        return -ENODEV;
    }

    res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "qtimer_reg");

    if (res) {
        qtmraddr = res->start;
        qtmraddr_size = resource_size(res);
        PTP_VIRTUAL_ALERT("%s: read qtmraddr: 0x%0llx size: 0x%llx\n",
                          __func__, qtmraddr, qtmraddr_size);
    } else {
        PTP_VIRTUAL_ALERT("%s: get ptp_virtual qtimer_reg resource failed\n", __func__);
        return -ENODEV;
    }

    PTP_VIRTUAL_DEBUG_L1("Exit: %s\n", __func__);
    return ret;
}

#endif
/*Enable below macros for Makena platform*/
#define MAC_STSR_RgOffAddr ((volatile uint32_t *)(virtbase + 0xb08))
#define MAC_STNSR_RgOffAddr ((volatile uint32_t *)(virtbase + 0xb0c))

/*Enable below macros for Hana platform*/
//#define MAC_STSR_RgOffAddr ((volatile uint32_t *)(virtbase - 4))
//#define MAC_STNSR_RgOffAddr ((volatile uint32_t *)(virtbase))

#define GET_VALUE(data, lbit, hbit) ((data >>lbit) & (~(~0<<(hbit-lbit+1))))
static int PTP_VM_get_time_with_pass_through(struct ptp_clock_info *ptp,
        struct timespec64 *ts)
{
#ifdef MEASURE_ROUNDTRIP_TIME
    struct timespec64 before;
    struct timespec64 after;
    unsigned long long before_ns;
    unsigned long long after_ns;
#endif
    uint64_t ns1 = 0, ns2 = 0;
    uint64_t varmac_stnsr = 0;
    uint64_t varmac_stsr = 0;
    uint64_t varmac_stsr_pre = 0;
    int32_t diff_gptp_time = 0;
#ifdef QTIMER_PASS_THRU
    unsigned long long cur_gptp = 0;
    uint32_t reminder;
    uint32_t qtimer_sync_lo = 0, qtimer_sync_hi = 0;
    uint32_t gptp_sync_lo = 0, gptp_sync_hi = 0;
    uint32_t qtimer_inc_ratio_lo = 0, qtimer_inc_ratio_hi = 0;
    long long qtimer_inc_ratio = 0;
    long long qtimer_sync_val = 0;
    long long gptp_sync_val = 0;
    uint64_t cur_qtimer_ns = 0;
    uint64_t *cur_gptp_mem;
    uint64_t *monolithic_mem;
    uint64_t *boot_time_mem;
    ktime_t after, before;
    ktime_t after_boot_time, before_boot_time;
    uint64_t after64, before64, monodiff;
    uint64_t after_boottime_64, before_boottime_64, bootdiff;
    uint32_t a_lock1_mem;
    uint32_t a_lock2_mem;
    uint32_t d_status;
    int count = 0;
    int mem_count = 0;

    if (ts == NULL) {
        PTP_VIRTUAL_ALERT("%s invalid timespec ts value\n", __func__);
        return -EFAULT;
    }

    cur_gptp_mem = (uint64_t *) (gptp_virtbase + 0x1000 - 3 * sizeof(uint64_t));
    monolithic_mem = (uint64_t *) (gptp_virtbase + 0x1000 - 4 * sizeof(uint64_t));
    boot_time_mem = (uint64_t *) (gptp_virtbase + 0x1000 - 9 * sizeof(uint64_t));
    d_status = ioread32((void *)((volatile uint32_t *)(gptp_virtbase + 0x1000 - 10 *
                                 sizeof(uint64_t))));

    if ( d_status != 0xabcdef ) {
        ts->tv_sec = 0;
        ts->tv_nsec = 0;
        iowrite64(0, monolithic_mem);
        iowrite64(0, cur_gptp_mem);
        PTP_VIRTUAL_ALERT("%s: ptp daemon not yet started ", __func__);
        return -ENODATA;
    }

    do {
        a_lock1_mem = ioread32((void *)((volatile uint32_t *)(gptp_virtbase + 0x1000 - 7
                                        * sizeof(uint64_t) - sizeof(uint32_t))));
        gptp_sync_lo = ioread32((void *)(((volatile uint32_t *)(
                                              gptp_virtbase + 0x1000 - 5 * sizeof(uint64_t)))));
        gptp_sync_hi = ioread32((void *)(((volatile uint32_t *)(
                                              gptp_virtbase + 0x1000 - 5 * sizeof(uint64_t) + 4))));
        qtimer_sync_lo = ioread32((void *)(((volatile uint32_t *)(
                                                gptp_virtbase + 0x1000 - 6 * sizeof(uint64_t)))));
        qtimer_sync_hi = ioread32((void *)(((volatile uint32_t *)(
                                                gptp_virtbase + 0x1000 - 6 * sizeof(uint64_t) + 4))));
        qtimer_inc_ratio_lo = ioread32((void *)(((volatile uint32_t *)(
                gptp_virtbase + 0x1000 - 7 * sizeof(uint64_t)))));
        qtimer_inc_ratio_hi = ioread32((void *)(((volatile uint32_t *)(
                gptp_virtbase + 0x1000 - 7 * sizeof(uint64_t) + 4))));
        a_lock2_mem = ioread32((void *)((volatile uint32_t *)(gptp_virtbase + 0x1000 - 7
                                        * sizeof(uint64_t) - 2 * sizeof(uint32_t))));
        mem_count++;
    } while ((a_lock1_mem != a_lock2_mem) && (mem_count < 3));

    do {
        local_irq_disable();
        before_boot_time = ktime_get_boottime();
        before = ktime_get();
        cur_qtimer_ns = ioread64((void *)(((volatile uint64_t * )(
                                               qtimer_virtbase)))) ; //qtimer ticks
        after = ktime_get();
        after_boot_time = ktime_get_boottime();
        local_irq_enable();
        before64 = ktime_to_ns(before);
        after64 = ktime_to_ns(after);
        monodiff = after64 - before64;
        before_boottime_64 = ktime_to_ns(before_boot_time);
        after_boottime_64 = ktime_to_ns(after_boot_time);
        bootdiff = after_boottime_64 - before_boottime_64;
        count++;
    } while (((monodiff > 15000) || (bootdiff > 15000)) && count < 10);

    //multiple by the tick factor.  (1000000000/19200000)
    cur_qtimer_ns = cur_qtimer_ns * 100;
    cur_qtimer_ns = div64_u64(cur_qtimer_ns, 192);
    cur_qtimer_ns = cur_qtimer_ns * 100;

    if (count >= 10 && ((monodiff > 15000) || (bootdiff > 15000))) {
        ts->tv_sec = 0;
        ts->tv_nsec = 0;
        cur_gptp = 0;
    } else {
        gptp_sync_val = (long long)((unsigned long long)gptp_sync_lo | ((
                                        unsigned long long)gptp_sync_hi << 32));
        qtimer_sync_val = (long long)((unsigned long long)qtimer_sync_lo | ((
                                          unsigned long long)qtimer_sync_hi << 32));
        qtimer_inc_ratio = (long long)((unsigned long long)qtimer_inc_ratio_lo | ((
                                           unsigned long long)qtimer_inc_ratio_hi << 32));
        cur_gptp = (unsigned long long)(gptp_sync_val + div64_u64(((cur_qtimer_ns -
                                        qtimer_sync_val) * qtimer_inc_ratio), 1000000000));
        ts->tv_sec = div_u64_rem(cur_gptp, 1000000000ULL, &reminder);
        ts->tv_nsec = reminder;
    }

    iowrite64(before64 + div64_u64(monodiff, 2), monolithic_mem);
    iowrite64(before_boottime_64 + div64_u64(bootdiff, 2), boot_time_mem);
    iowrite64(cur_gptp, cur_gptp_mem);
    PTP_VIRTUAL_DEBUG_L1("%s: cur_gptp_ns: %lld cur_qtimer_ns %lld monotonic_time_ns %lld boot_time_ns %lld gptp_sync_val %lld qtimer_sync_val %lld d_status %x",
                         __func__, cur_gptp, cur_qtimer_ns, before64 + div64_u64(monodiff, 2),
                         before_boottime_64 + div64_u64(bootdiff, 2), gptp_sync_val,  qtimer_sync_val,
                         d_status);

    if (cur_gptp == 0) {
        PTP_VIRTUAL_ALERT("%s: unable to fetch current gptp time %llu", __func__,
                          cur_gptp);
        return -EINVAL;
    }

    return 0;
#endif
    PTP_VIRTUAL_DEBUG_L1("Entry: %s.\n", __func__);
#ifdef MEASURE_ROUNDTRIP_TIME
    getnstimeofday(&before);
#endif

    do {
        /* Read the seconds */
        varmac_stsr_pre = (uint64_t)ioread32((void *)MAC_STSR_RgOffAddr);
        /* Read the nanoseconds once */
        varmac_stnsr = (uint64_t)ioread32((void *)MAC_STNSR_RgOffAddr);
        ns1 = GET_VALUE(varmac_stnsr, 0, 30);
        /* Read the seconds */
        varmac_stsr = (uint64_t)ioread32((void *)MAC_STSR_RgOffAddr);
    } while (varmac_stsr_pre != varmac_stsr);

#ifdef MEASURE_ROUNDTRIP_TIME
    getnstimeofday(&after);
    before_ns = before.tv_sec * 1000000000LL + before.tv_nsec;
    after_ns = after.tv_sec * 1000000000LL + after.tv_nsec;
    PTP_VIRTUAL_DEBUG_L1("%s: vdev read finished in (%lld)nano sec\n", __func__,
                         (after_ns - before_ns));
#endif
    PTP_VIRTUAL_DEBUG_L1("%s: varmac_stsr is %d varmac_stnsr is %d \n", __func__,
                         varmac_stsr, varmac_stnsr);
    ts->tv_sec = varmac_stsr;
    ts->tv_nsec = ns1;
    ns2 = varmac_stsr * 1000000000LL + ns1;
    diff_gptp_time = ns2 - cur_gptp;
    PTP_VIRTUAL_ALERT("%s: gptp_reg_time %lldns carveout_gptp_time = %lldns diff = %dns\n",
                      __func__, ns2, cur_gptp, diff_gptp_time);
    PTP_VIRTUAL_DEBUG_L1("Exit: %s.\n", __func__);
    return 0;
}
#endif

/*static int PTP_VM_adjust_freq(struct ptp_clock_info *ptp, s32 ppb)
{
    PTP_VIRTUAL_DEBUG_L1("Entry: %s.\n", __func__);
    PTP_VIRTUAL_DEBUG_L1("Exit: %s.\n", __func__);
    return 0;
}*/

static int PTP_VM_adjust_time(struct ptp_clock_info *ptp, s64 delta)
{
    PTP_VIRTUAL_DEBUG_L1("Entry: %s.\n", __func__);
    PTP_VIRTUAL_DEBUG_L1("Exit: %s.\n", __func__);
    return 0;
}


static int PTP_VM_set_time(struct ptp_clock_info *ptp,
                           const struct timespec64 *ts)
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
    .n_alarm = 0,   /* the number of programmable alarms */
    .n_ext_ts = 0,  /* the number of externel time stamp channels */
    .n_per_out = 0, /* the number of programmable periodic signals */
    .pps = 0,   /* indicates whether the clk supports a PPS callback */
    //.adjfreq = PTP_VM_adjust_freq,
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

#ifdef GPTP_GVM_SHM_MODE
static resource_size_t carveout_memory_address = 0;
static resource_size_t carveout_memory_size = 0;
static int ptp_virtual_get_dts_config(struct platform_device *pdev)
{
    int32_t ret = 0;
    struct resource *res;
    PTP_VIRTUAL_DEBUG_L1("Entry: %s.\n", __func__);
    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

    if (res) {
        carveout_memory_address = res->start;
        carveout_memory_size = resource_size(res);
        PTP_VIRTUAL_ALERT("%s: read reserved-addr: 0x%0llx size: 0x%llx\n",
                          __func__, carveout_memory_address, carveout_memory_size);
    } else {
        PTP_VIRTUAL_ALERT("%s: get ptp_virtual register resource failed\n", __func__);
        return -ENODEV;
    }

    PTP_VIRTUAL_DEBUG_L1("Exit: %s\n", __func__);
    return ret;
}
#endif

static int ptp_virtual_probe(struct platform_device *pdev)
{
    int ret = 0;
    PTP_VIRTUAL_DEBUG_L1("Entry: %s\n", __func__);
#ifdef GPTP_GVM_SHM_MODE
    ret = ptp_virtual_get_dts_config(pdev);
#endif
#ifdef PTP_READ_WTIH_VDEV
    ptp_vm_clk = ptp_clock_register(&PTP_VM_ptp_clock_ops, NULL);

    if (IS_ERR(ptp_vm_clk)) {
        PTP_VIRTUAL_ALERT("%s: ptp clock registratin failed\n", __func__);
        ptp_vm_clk = NULL;
    } else {
        PTP_VIRTUAL_ALERT("%s: ptp clock registered:\n", __func__);
    }

#endif
#ifdef PTP_READ_WTIH_SOCKET
    ret = ptp_virtual_setup_socket();
#endif
#ifdef PTP_READ_WTIH_VDEV
    ret = ptp_virtual_setup_vdev(pdev->dev.of_node);
#endif
#ifdef PTP_READ_WTIH_PASS_THRU
    ret = ptp_virtual_setup_passthrough(pdev);
#endif
    PTP_VIRTUAL_DEBUG_L1("Exit: %s\n", __func__);
    return ret;
}

static int  ptp_virtual_remove(struct platform_device *pdev)
{
    int ret = 0;
    PTP_VIRTUAL_DEBUG_L1("Entry:%s.\n", __func__);
#ifdef PTP_READ_WTIH_VDEV
    ptp_clock_unregister(ptp_vm_clk);
    ptp_vm_clk = NULL;
#endif
    PTP_VIRTUAL_DEBUG_L1("Exit:%s.\n", __func__);
    return ret;
}

static struct of_device_id ptp_virtual_match[] = {
    {
        .compatible = "qcom,ptp_virtual",
    },
    {}
};

static struct platform_driver ptp_virtual_driver = {
    .probe  = ptp_virtual_probe,
    .remove = ptp_virtual_remove,
    .driver = {
        .name       = "ptp_virtual",
        .owner      = THIS_MODULE,
        .of_match_table = ptp_virtual_match,
    },
};

#ifdef GPTP_GVM_SHM_MODE
static int gptp_shm_mmap(struct file *file, struct vm_area_struct *vma)
{
    unsigned long pfn_start = (unsigned long)(carveout_memory_address >> PAGE_SHIFT)
                              + vma->vm_pgoff;
    unsigned long size = vma->vm_end - vma->vm_start;
    int ret = 0;
    vma->vm_page_prot = PAGE_SHARED;
    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

    if (size > carveout_memory_size) {
        pr_err("MMAP size exceeds reserved memory size");
        return -EINVAL;
    }

    ret = remap_pfn_range(vma, vma->vm_start, pfn_start, size, vma->vm_page_prot);

    if (ret)
        printk("%s: remap_pfn_range failed at [0x%lx  0x%lx]\n",
               __func__, vma->vm_start, vma->vm_end);
    else
        printk("%s: map 0x%lx to 0x%lx, size: 0x%lx\n", __func__, pfn_start,
               vma->vm_start, size);

    return ret;
}

static const struct file_operations gptp_shm_fops = {
    .owner = THIS_MODULE,
    .mmap = gptp_shm_mmap,
};

static struct miscdevice gptp_shm_misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "gptp_shm",
    .fops = &gptp_shm_fops,
};

/*
static int qtimer_shm_mmap(struct file *file, struct vm_area_struct *vma)
{
        unsigned long pfn_start = (unsigned long)(QTMR0_F0V2_QTMR_V2_ADDR >> PAGE_SHIFT) + vma->vm_pgoff;
        unsigned long size = vma->vm_end - vma->vm_start;
        int ret = 0;

        vma->vm_page_prot = PAGE_SHARED;
        vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
        if (size > QTMR0_F0V2_QTMR_V2_SIZE) {
                pr_err("MMAP qtimer size exceeds reserved memory size");
                return -EINVAL;
        }
        ret = remap_pfn_range(vma, vma->vm_start, pfn_start, size, vma->vm_page_prot);
        if (ret)
                printk("%s: remap_pfn_range qtimer failed at [0x%lx  0x%lx]\n",
                                __func__, vma->vm_start, vma->vm_end);
        else
                printk("%s: map 0x%lx to 0x%lx, size: 0x%lx\n", __func__, pfn_start,
                                vma->vm_start, size);

        return ret;
}

static const struct file_operations qtimer_shm_fops = {
        .owner = THIS_MODULE,
        .mmap = qtimer_shm_mmap,
};

static struct miscdevice qtimer_shm_misc = {
        .minor = MISC_DYNAMIC_MINOR,
        .name = "qtimer_shm",
        .fops = &qtimer_shm_fops,
};
*/
#endif

static int __init ptp_virtual_init(void)
{
    int ret = 0;
    PTP_VIRTUAL_DEBUG_L1("Entry:%s.\n", __func__);
    ret = platform_driver_register(&ptp_virtual_driver);

    if (unlikely(ret)) {
        pr_err("failed to register platform driver!\n");
    } else {
        printk("platform_driver_register OK.\r\n");
    }

#ifdef GPTP_GVM_SHM_MODE
    ret = misc_register(&gptp_shm_misc);

    if (unlikely(ret)) {
        pr_err("failed to register misc device!\n");
    } else {
        printk("misc_register OK.\r\n");
    }

    /*
        ret = misc_register(&qtimer_shm_misc);
        if (unlikely(ret)) {
            pr_err("failed to register qtimer misc device!\n");
        } else {
            printk("misc_register qtimer OK.\r\n");
        }
    */
#endif
#ifdef PTP_READ_WTIH_PASS_THRU

    if (!ntnbase) {
        virtbase = ioremap(NTN_BASE_ADDRESS, 0x1000);
    } else {
        virtbase = ioremap(ntnbase, 0x1000);
    }

    PTP_VIRTUAL_ALERT("%s: virtbase: %p %0llx\n", __func__, virtbase, ntnbase);
#ifdef QTIMER_PASS_THRU

    if (!qtmraddr) {
        qtimer_virtbase = ioremap(QTMR0_F0V2_QTMR_V2_ADDR, QTMR0_F0V2_QTMR_V2_SIZE);
    } else {
        qtimer_virtbase = ioremap(qtmraddr, QTMR0_F0V2_QTMR_V2_SIZE);
    }

    PTP_VIRTUAL_ALERT("%s: qtimer_virtbase: %p %0llx\n", __func__, qtimer_virtbase,
                      qtmraddr);

    if (carveout_memory_address) {
        gptp_virtbase = memremap(carveout_memory_address, 0x1000,
                                 MEMREMAP_WB);//ioremap(carveout_memory_address, 0x1000);
        PTP_VIRTUAL_ALERT("%s: gptp_virtbase memremap: %p %0llx\n", __func__,
                          gptp_virtbase,
                          carveout_memory_address);
    }

    if (!qtimer_virtbase || !gptp_virtbase) {
        PTP_VIRTUAL_ALERT("%s: ioremap qtimer and gptp memory failed\n", __func__);
    }

#endif
    ptp_vm_clk = ptp_clock_register(&PTP_VM_ptp_clock_ops, NULL);

    if (IS_ERR(ptp_vm_clk)) {
        PTP_VIRTUAL_ALERT("%s: ptp clock registratin failed\n", __func__);
        ptp_vm_clk = NULL;
    } else {
        PTP_VIRTUAL_ALERT("%s: ptp clock registered:\n", __func__);
    }

#endif
    PTP_VIRTUAL_DEBUG_L1("Exit:%s.\n", __func__);
    return ret;
}

static void __exit ptp_virtual_exit(void)
{
    PTP_VIRTUAL_DEBUG_L1("Entry:%s.\n", __func__);
#ifdef PTP_READ_WTIH_SOCKET
    ptp_virtual_disconnect_socket();
#endif
#ifdef PTP_READ_WTIH_VDEV
    ptp_virtual_disconnect_vdev();
#endif
    platform_driver_unregister(&ptp_virtual_driver);
#ifdef PTP_READ_WTIH_PASS_THRU
    ptp_clock_unregister(ptp_vm_clk);
    ptp_vm_clk = NULL;
    iounmap(virtbase);
    virtbase = NULL;
#ifdef QTIMER_PASS_THRU
    iounmap(qtimer_virtbase);
    virtbase = NULL;
    iounmap(gptp_virtbase);
    virtbase = NULL;
#endif
#endif
#ifdef GPTP_GVM_SHM_MODE
    misc_deregister(&gptp_shm_misc);
#endif
    PTP_VIRTUAL_DEBUG_L1("Exit:%s.\n", __func__);
}

module_init(ptp_virtual_init);
module_exit(ptp_virtual_exit);
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, ptp_virtual_match);
