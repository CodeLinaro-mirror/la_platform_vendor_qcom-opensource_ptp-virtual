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

#ifndef __VDEV_VIRTIO_EAVB_H__
#define __VDEV_VIRTIO_EAVB_H__


//#include <stdint.h>
//#include <stdlib.h>

#define IF_NAMESIZE             16
#define STATION_ADDR_SIZE       8
#define MAX_STREAM_NUM          8
#define MAX_CONFIG_FILE_PATH    512

#define VERSION_MAJOR           0
#define VERSION_MINOR           1

#define VIRTIO_EAVB_T_CREATE_STREAM     0
#define VIRTIO_EAVB_T_GET_STREAM_INFO   1
#define VIRTIO_EAVB_T_CONNECT_STREAM    2
#define VIRTIO_EAVB_T_RECEIVE           3
#define VIRTIO_EAVB_T_TRANSMIT          4
#define VIRTIO_EAVB_T_DISCONNECT_STREAM 5
#define VIRTIO_EAVB_T_DESTROY_STREAM    6
#define VIRTIO_EAVB_T_CREATE_STREAM_PATH    7
#define VIRTIO_EAVB_T_VERSION           8
#define VIRTIO_EAVB_T_MMAP              9
#define VIRTIO_EAVB_T_MUNMAP            10
#define VIRTIO_EAVB_T_UPDATE_CLK        11

struct vio_msg_hdr {
	uint16_t msgid;          /* unique message id */
	uint16_t len;            /* command total length */
	uint32_t cmd;            /* command */
	uint64_t streamctx_hdl;  /* streamCtx handle */
	int32_t stream_idx;     /* streamCtx idx in BE */
	int32_t result;         /* command result */
}__attribute__ ((__packed__));


/*
 * EAVB_IOCTL_CREATE_STREAM
 */

// TODO: remove
struct qavb_stream_config {
	//common
	uint16_t stream_id;
	char eth_interface[IF_NAMESIZE];
	uint16_t vlan_id;
	uint16_t ring_buffer_elem_count;
	int ring_buffer_mode;
	int avb_role;      // talker = 0 or listener = 1
	uint8_t dest_macaddr[STATION_ADDR_SIZE];
	uint8_t stream_addr[STATION_ADDR_SIZE];
	uint8_t crf_dest_macaddr[STATION_ADDR_SIZE];    // "crf_macaddr" or "crf_dest_macaddr"
	uint8_t crf_stream_addr[STATION_ADDR_SIZE];
	int mapping_type;
	int wakeup_interval; // "wakeup_interval" or "tx_interval"
	int tx_pkts_per_sec; //if not set, do default
	int max_stale_ms;     //int max_stale_ns = max_stale_ms*1000;
	int presentation_time_ms; //if not set, do default
	int enforce_presentation_time;
	int sr_class_type; //A = 0  B = 1 AAF = 2
	int packing_factor; // sets number of items to se sent on each tx / rx
	int bandwidth;

	// H.264
	int max_payload;    // "max_payload" or "max_video_payload"

	int mrp_enabled;

	//Audio Specific
	int pcm_bit_depth;
	int pcm_channels;
	int sample_rate; //in hz
	unsigned char endianness;       // 0 = big 1 = little

	int ieee1722_standard;

	// Thread priority in QNX side 
	// TODO: BE or FE
	int talker_priority;
	int listener_priority;
	int crf_priority;

	// CRF
	int crf_mode;               // 0 - disabled
								// 1 - CRF talker (listener drives reference)
								// 2 - CRF with talker reference (talker has CRF talker)

	int crf_type;               // 0 - custom
								// 1- audio
								// 2- video frame
								// 3 - video line
								// 4 - machine cycle

	int crf_timestamping_interval; // time interval after how many events timestamp is to be                   produced
	int crf_timestamping_interval_remote; // time interval after how many events timestamp is to               be            produced
								// (base_frequency * pull) / timestamp_interval =
								// # of timestamps per second
	int crf_timestamping_interval_local;
	int crf_allow_dynamic_tx_adjust; // enables/disables dynamic IPG adjustments
	int crf_num_timestamps_per_pkt;   // indicates how many CRF timestamps per each CRF packet

	int64_t crf_mcr_adjust_min_ppm;
	int64_t crf_mcr_adjust_max_ppm;

	uint16_t crf_stream_id;     // CRF stream ID
	int32_t crf_base_frequency; // CRF base frequency

	int32_t crf_listener_ts_smoothing;
	int32_t crf_talker_ts_smoothing;

	int crf_pull;              // multiplier for the base frequency;
	int crf_event_callback_interval;   // indicates how often to issue MCR callback events
							   // how many packets will generate one callback.
	int crf_dynamic_tx_adjust_interval; // Indicates how often to update IPG

	// stats
	int32_t enable_stats_reporting;
	int32_t stats_reporting_interval;
	int32_t stats_reporting_samples;

	// packet tracking
	int32_t enable_packet_tracking;
	int32_t packet_tracking_interval;
	int blocking_write_enabled;
	double blocking_write_fill_level;
	int app_tx_block_enabled;
	int stream_interleaving_enabled;
	int create_talker_thread;
	int create_crf_threads;
	int listener_bpf_pkts_per_buff;
};

struct vio_stream_config {
    //common
    uint16_t stream_id; 
    char eth_interface[IF_NAMESIZE];
    uint16_t vlan_id;
    uint16_t ring_buffer_elem_count;
    int ring_buffer_mode;
    int avb_role;      // talker = 0 or listener = 1
    uint8_t dest_macaddr[STATION_ADDR_SIZE];
    uint8_t stream_addr[STATION_ADDR_SIZE];
    uint8_t crf_dest_macaddr[STATION_ADDR_SIZE];    // "crf_macaddr" or "crf_dest_macaddr"
    uint8_t crf_stream_addr[STATION_ADDR_SIZE];
    int mapping_type;
    int wakeup_interval; // "wakeup_interval" or "tx_interval"
    int tx_pkts_per_sec; //if not set, do default
    int max_stale_ms;     //int max_stale_ns = max_stale_ms*1000;
    int presentation_time_ms; //if not set, do default
    int enforce_presentation_time;
    int sr_class_type; //A = 0  B = 1 AAF = 2
    int packing_factor; // sets number of items to se sent on each tx / rx
    int bandwidth;

    // H.264
    int max_payload;    // "max_payload" or "max_video_payload"

    int mrp_enabled;

    //Audio Specific
    int pcm_bit_depth;
    int pcm_channels;
    int sample_rate; //in hz
    unsigned char endianness;       // 0 = big 1 = little

    int ieee1722_standard;

    // Thread priority in QNX side 
    // TODO: BE or FE
    int talker_priority;
    int listener_priority;
    int crf_priority;

    // CRF
    int crf_mode;               // 0 - disabled
                                // 1 - CRF talker (listener drives reference)
                                // 2 - CRF with talker reference (talker has CRF talker)

    int crf_type;               // 0 - custom
                                // 1- audio
                                // 2- video frame
                                // 3 - video line
                                // 4 - machine cycle

    int crf_timestamping_interval; // time interval after how many events timestamp is to be                   produced
    int crf_timestamping_interval_remote; // time interval after how many events timestamp is to               be            produced
                                // (base_frequency * pull) / timestamp_interval =
                                // # of timestamps per second
    int crf_timestamping_interval_local;
    int crf_allow_dynamic_tx_adjust; // enables/disables dynamic IPG adjustments
    int crf_num_timestamps_per_pkt;   // indicates how many CRF timestamps per each CRF packet

    int64_t crf_mcr_adjust_min_ppm;
    int64_t crf_mcr_adjust_max_ppm;

    uint16_t crf_stream_id;     // CRF stream ID
    int32_t crf_base_frequency; // CRF base frequency

    int32_t crf_listener_ts_smoothing;
    int32_t crf_talker_ts_smoothing;

    int crf_pull;              // multiplier for the base frequency;
    int crf_event_callback_interval;   // indicates how often to issue MCR callback events
                               // how many packets will generate one callback.
    int crf_dynamic_tx_adjust_interval; // Indicates how often to update IPG

    // stats
    int32_t enable_stats_reporting;
    int32_t stats_reporting_interval;
    int32_t stats_reporting_samples;

    // packet tracking
    int32_t enable_packet_tracking;
    int32_t packet_tracking_interval;
    int blocking_write_enabled;
    double blocking_write_fill_level;
    int app_tx_block_enabled;
    int stream_interleaving_enabled;
    int create_talker_thread;
    int create_crf_threads;
    int listener_bpf_pkts_per_buff;
}__attribute__((__packed__));


struct vio_create_stream_msg {
    struct vio_msg_hdr mhdr;
    struct vio_stream_config cfg;
    uint64_t streamCtx;
    int32_t stream_idx;
}__attribute__((__packed__));


struct vio_create_stream_path_msg {
    struct vio_msg_hdr mhdr;
    char path[MAX_CONFIG_FILE_PATH];
    uint64_t streamCtx;
    int32_t stream_idx;
}__attribute__((__packed__));



/*
 * EAVB_IOCTL_GET_STREAM_INFO
 */
   
struct eavb_stream_info { 
    //common
    int role;
    int mapping_type;
    unsigned int max_payload;      /* Max packet payload size */
    unsigned int pkts_per_wake;    /* Number of packets sent per wake */
    unsigned int wakeup_period_us; /* Time to sleep between wakes */

    //Audio Specific
    int pcm_bit_depth;             /* Audio bit depth 8/16/24/32 */
    int num_pcm_channels;          /* Audio channels 1/2 */
    int sample_rate;               /* Audio sample rate in hz */
    unsigned char endianness;      /* Audio sample endianness 0(big)/1(little) */
    
    unsigned int max_buffer_size;         /* Max buffer size (Bytes) allowed */
}__attribute__((__packed__));


struct vio_get_stream_info_msg {
    struct vio_msg_hdr mhdr;
    struct eavb_stream_info stream_info;
}__attribute__((__packed__));


/*
 * EAVB_IOCTL_CONNECT_STREAM
 */

struct vio_connect_stream_msg {
    struct vio_msg_hdr mhdr;
}__attribute__((__packed__));



/*
 * EAVB_IOCTL_RECEIVE
 */

struct eavb_buf_hdr {
    uint32_t flag_end_of_frame:1;   // This flag is used for H.264 and MJPEG streams:
                                    // 1. H.264: Set for the very last packet of an access unit. 
                                    // 2. MJPEG  Set the last packet of a video frame. 
                                    // 
                                    //****************************************************************
    uint32_t flag_end_of_file:1;    // This flag is used in file transfer only:
                                    // Set for the last packet in the file 
                                    //****************************************************************
    uint32_t flag_reserved:30;
                                    //****************************************************************
    uint32_t event;                 // Audio event      Layout D3scription      Valid Channels
                                    // event value
                                    //  0               Static layout           Based on config
                                    //  1               Mono                    0               
                                    //  2               Stereo                  0, 1
                                    //  3               5.1                     0,1,2,3,4,5
                                    //  4               7.1                     0,1,2,3,4,5,6,7
                                    //  5-15            Custom                  Defined by System 
                                    //                                          Integrator
                                    //****************************************************************
    uint32_t reserved;
    uint32_t payload_size;          // Size of the payload (bytes)
}__attribute__((__packed__));

struct eavb_buf_data {
    struct eavb_buf_hdr hdr;
    uint64_t gpa;                       /* GVM physical address of buffer */
}__attribute__((__packed__));

struct vio_receive_msg {
    struct vio_msg_hdr mhdr;
    struct eavb_buf_data data; //IN/OUT
    int32_t received;
}__attribute__((__packed__));


/*
 * EAVB_IOCTL_TRANSMIT
 */

struct vio_transmit_msg {
    struct vio_msg_hdr mhdr;
    struct eavb_buf_data data; //IN/OUT
    int32_t written;
}__attribute__((__packed__));


/*
 * EAVB_IOCTL_DISCONNECT_STREAM
 */

struct vio_disconnect_stream_msg {
    struct vio_msg_hdr mhdr;
}__attribute__((__packed__));


/*
 * EAVB_IOCTL_DESTROY_STREAM
 */

struct vio_destroy_stream_msg {
    struct vio_msg_hdr mhdr;
}__attribute__((__packed__));

/*
 * EAVB_IOCTL_VERSION
 */
struct vio_version_msg {
	struct vio_msg_hdr mhdr;	//IN
	uint16_t major;			//IN
	uint16_t minor;			//IN
}__attribute__((__packed__));

/*
 * EAVB_IOCTL_MMAP
 */

struct vio_mmap_msg {
	struct vio_msg_hdr mhdr;	//IN
	uint32_t size;			//IN
	uint64_t gpa;			//IN
}__attribute__((__packed__));

/*
 * EAVB_IOCTL_MUNMAP
 */
struct vio_munmap_msg {
	struct vio_msg_hdr mhdr;	//IN
	uint64_t gpa;			//IN
}__attribute__((__packed__));

/*
 * EAVB_IOCTL_UPDATE_CLK
 */
struct vio_update_clk_msg {
	struct vio_msg_hdr mhdr;	//IN
	uint64_t clk;			//IN
}__attribute__((__packed__));

#endif /* __VDEV_VIRTIO_EAVB_H__ */
