#include <linux/types.h>

#define SND_AVB_DRIVER		"snd_avb"

/* #define AVB_DEBUG */

#define ETH_MSRP                (0x22EA)

#define SND_AVB_NUM_CARDS	(SNDRV_CARDS)

#ifdef CONFIG_PM_SLEEP
#define AVB_PM_OPS	&avb_pm
#else
#define AVB_PM_OPS	NULL
#endif

#define AVB_USE_HIGH_RES_TIMER

#define AVB_KERN_EMERG 0	/* system is unusable */
#define AVB_KERN_ALERT 1	/* action must be taken immediately */
#define AVB_KERN_CRIT  2	/* critical conditions */
#define AVB_KERN_ERR   3	/* error conditions */
#define AVB_KERN_WARN  4	/* warning conditions */
#define AVB_KERN_NOT   5	/* normal but significant condition */
#define AVB_KERN_INFO  6	/* informational */
#define AVB_KERN_DEBUG 7	/* debug-level messages */

#define AVB_WQ "AVBWQ"

#define AVB_DELAY_WORK_MSRP                             (0)
#define AVB_DELAY_WORK_AVTP                             (1)
#define AVB_DELAY_WORK_AVDECC                           (2)

#define AVB_MAX_TS_SLOTS                                (12)
#define AVB_AVTP_AAF_SAMPLES_PER_PACKET		        (192)    /* 4ms * 48KHz i.e. Maxframes per jiffy for HZ=250 */
#define AVB_MSRP_ETH_FRAME_SIZE                         (2048)
#define AVB_MAX_ETH_FRAME_SIZE                          (AVB_MSRP_ETH_FRAME_SIZE)

#define MSRP_ATTRIBUTE_TYPE_TALKER_ADVERTISE_VECTOR	(1)
#define MSRP_ATTRIBUTE_TYPE_TALKER_FAILED_VECTOR	(2)
#define MSRP_ATTRIBUTE_TYPE_LISTENER_VECTOR		(3)
#define MSRP_ATTRIBUTE_TYPE_DOMAIN_VECTOR		(4)

#define MSRP_ATTRIBUTE_LEN_TALKER_ADVERTISE_VECTOR	(25)
#define MSRP_ATTRIBUTE_LEN_TALKER_FAILED_VECTOR		(34)
#define MSRP_ATTRIBUTE_LEN_LISTENER_VECTOR		(8)
#define MSRP_ATTRIBUTE_LEN_DOMAIN_VECTOR		(4)

#define MSRP_MAX_FRAME_SIZE_48KHZ_AUDIO                 (80)

#define MSRP_MAX_INTERVAL_FRAME_48KHZ_AUDIO             (1)

#define MSRP_ATTRIBUTE_EVENT_NEW                        (0)
#define MSRP_ATTRIBUTE_EVENT_JOININ                     (1)
#define MSRP_ATTRIBUTE_EVENT_IN                         (2)
#define MSRP_ATTRIBUTE_EVENT_JOINMT                     (3)
#define MSRP_ATTRIBUTE_EVENT_MT                         (4)
#define MSRP_ATTRIBUTE_EVENT_LEAVE                      (5)

#define MSRP_DECLARATION_STATE_NONE                     (0)
#define MSRP_DECLARATION_STATE_IGNORE                   (0)
#define MSRP_DECLARATION_STATE_ASKING_FAILED            (1)
#define MSRP_DECLARATION_STATE_READY                    (2)
#define MSRP_DECLARATION_STATE_READY_FAILED             (3)
#define MSRP_DECLARATION_STATE_UNKNOWN                  (255)

#define MSRP_THREE_PACK(a, b, c) (u8)((((a * 6) + b) * 6) + c)
#define MSRP_THREE_PACK_GET_A(x) (u8)(x/36)
#define MSRP_THREE_PACK_GET_B(x) (u8)((x/6) % 6)
#define MSRP_THREE_PACK_GET_C(x) (u8)(x%6)
#define MSRP_FOUR_PACK(a, b, c, d) (u8)((a * 64) + (b * 16) + (c * 4) + (d))
#define MSRP_FOUR_PACK_GET_A(x) (u8)(x/64)

#define AVTP_PDU_COMMON_STREAM_HEADER_LENGTH            (24)
#define AVTP_PDU_COMMON_CONTROL_HEADER_LENGTH           (12)

#define AVB_AVTP_AAF_VERSION			(0)

#define AVB_AVTP_SUBTYPE_AAF			(2)
#define AVB_AVTP_SUBTYPE_ADP			(0xFA)
#define AVB_AVTP_SUBTYPE_AECP			(0xFB)
#define AVB_AVTP_SUBTYPE_ACMP			(0xFC)
#define AVB_AVTP_SUBTYPE_MAAP                   (0xFE)

#define AVB_AVTP_AAF_FORMAT_USER_SP		(0)
#define AVB_AVTP_AAF_FORMAT_32_BIT_FLOAT	(1)
#define AVB_AVTP_AAF_FORMAT_32_BIT_INT		(2)
#define AVB_AVTP_AAF_FORMAT_24_bit_INT		(3)
#define AVB_AVTP_AAF_FORMAT_16_BIT_INT		(4)
#define AVB_AVTP_AAF_FORMAT_32_BIT_AES3		(5)

#define AVB_AVTP_AAF_NSR_USER_SP		(0x0)
#define AVB_AVTP_AAF_NSR_8_KHZ			(0x1)
#define AVB_AVTP_AAF_NSR_16_KHZ			(0x2)
#define AVB_AVTP_AAF_NSR_32_KHZ			(0x3)
#define AVB_AVTP_AAF_NSR_44_1_KHZ		(0x4)
#define AVB_AVTP_AAF_NSR_48_KHZ			(0x5)
#define AVB_AVTP_AAF_NSR_88_2_KHZ		(0x6)
#define AVB_AVTP_AAF_NSR_96_KHZ			(0x7)
#define AVB_AVTP_AAF_NSR_176_4_KHZ		(0x8)
#define AVB_AVTP_AAF_NSR_192_KHZ		(0x9)
#define AVB_AVTP_AAF_NSR_24_KHZ			(0xA)

#define AVB_ADP_MSGTYPE_ENTITY_AVAILABLE	(0x0)
#define AVB_ADP_MSGTYPE_ENTITY_DEPARTING	(0x1)
#define AVB_ADP_MSGTYPE_ENTITY_DISCOVER		(0x2)

#define AVB_ADP_CONTROL_DATA_LENGTH             (56)

#define AVB_AECP_MSGTYPE_AEM_COMMAND            (0x00)
#define AVB_AECP_MSGTYPE_AEM_RESPONSE           (0x01)

#define AVB_AEM_CMD_ENTITY_ACQUIRE              (0x00)
#define AVB_AEM_CMD_ENTITY_LOCK                 (0x01)
#define AVB_AEM_CMD_ENTITY_AVAILABLE            (0x02)
#define AVB_AEM_CMD_CTRL_AVAILABLE              (0x03)
#define AVB_AEM_CMD_READ_DESCP	                (0x04)
#define AVB_AEM_CMD_WRITE_DESCP                 (0x05)
#define AVB_AEM_CMD_SET_CONFIG                  (0x06)
#define AVB_AEM_CMD_GET_CONFIG                  (0x07)
#define AVB_AEM_CMD_SET_STREAM_FORMAT           (0x08)
#define AVB_AEM_CMD_SET_STREAM_INFO             (0x0e)
#define AVB_AEM_CMD_GET_STREAM_INFO             (0x0f)
#define AVB_AEM_CMD_REGISTER_UNSOLICITED_NOTIFICATION	(0x24)
#define AVB_AEM_CMD_GET_COUNTERS                (0x029)

#define AVB_AEM_DESCP_ENTITY			(0x00) 
#define AVB_AEM_DESCP_CONFIGURATION             (0x01)
#define AVB_AEM_DESCP_AUDIO_UNIT                (0x02)
#define AVB_AEM_DESCP_STREAM_IP                 (0x05) 
#define AVB_AEM_DESCP_STREAM_OP                 (0x06)
#define AVB_AEM_DESCP_JACK_IP                   (0x07)
#define AVB_AEM_DESCP_JACK_OP                   (0x08)
#define AVB_AEM_DESCP_AVBINTERFACE              (0x09)
#define AVB_AEM_DESCP_CLOCKSOURCE               (0x0a)
#define AVB_AEM_DESCP_LOCALE                    (0x0c)
#define AVB_AEM_DESCP_STRINGS                   (0x0d)
#define AVB_AEM_DESCP_STREAM_PORT_IP            (0x0e)
#define AVB_AEM_DESCP_STREAM_PORT_OP            (0x0f)
#define AVB_AEM_DESCP_EXT_PORT_IP               (0x10)
#define AVB_AEM_DESCP_EXT_PORT_OP               (0x11) 
#define AVB_AEM_DESCP_AUDIO_CLUSTER             (0x14)
#define AVB_AEM_DESCP_AUDIO_MAP                 (0x17)
#define AVB_AEM_DESCP_CLOCK_DOMAIN              (0x24)
#define AVB_AEM_DESCP_INVALID                   (0xffff)  

#define AVB_AEM_RES_SUCCESS                     (0x00) 
#define AVB_AEM_RES_NOT_IMPLEMENTED             (0x01)
#define AVB_AEM_RES_NO_SUCH_DESCRIPTOR          (0x02)    

#define AVB_AEM_MAX_DESCP_COUNT                 (7)  

#define AVB_AEM_STREAM_FORMAT_AVTP              (0x02)
#define AVB_AEM_MAX_SUPP_FORMATS                (6)

#define AVB_ACMP_MSGTYPE_CONNECT_TX_CMD         (0x00)
#define AVB_ACMP_MSGTYPE_CONNECT_TX_RESP        (0x01)
#define AVB_ACMP_MSGTYPE_DISCONNECT_TX_CMD      (0x02)
#define AVB_ACMP_MSGTYPE_DISCONNECT_TX_RESP     (0x03)
#define AVB_ACMP_MSGTYPE_GET_TX_STATE_CMD       (0x04)
#define AVB_ACMP_MSGTYPE_GET_TX_STATE_RESP      (0x05)
#define AVB_ACMP_MSGTYPE_CONNECT_RX_CMD         (0x06)
#define AVB_ACMP_MSGTYPE_CONNECT_RX_RESP        (0x07)
#define AVB_ACMP_MSGTYPE_DISCONNECT_RX_CMD      (0x08)
#define AVB_ACMP_MSGTYPE_DISCONNECT_RX_RESP     (0x09)
#define AVB_ACMP_MSGTYPE_GET_RX_STATE_CMD       (0x0A)
#define AVB_ACMP_MSGTYPE_GET_RX_STATE_RESP      (0x0B)
#define AVB_ACMP_MSGTYPE_GET_TX_CONN_CMD        (0x0C)
#define AVB_ACMP_MSGTYPE_GET_TX_CONN_RESP       (0x0D)

#define AVB_ACMP_STATUS_SUCCESS                   (0x00)
#define AVB_ACMP_STATUS_LISTENER_UNKNOWN_ID       (0x01)
#define AVB_ACMP_STATUS_TALKER_UNKNOWN_ID         (0x02)
#define AVB_ACMP_STATUS_TALKER_DEST_MAC_FAIL      (0x03)
#define AVB_ACMP_STATUS_TALKER_NO_STREAM_IDX      (0x04)
#define AVB_ACMP_STATUS_TALKER_NO_BANDWIDTH       (0x05)
#define AVB_ACMP_STATUS_TALKER_EXCLUSIVE          (0x06)
#define AVB_ACMP_STATUS_LISTENER_TALKER_TIMEOUT   (0x07)
#define AVB_ACMP_STATUS_LISTENER_EXCLUSIVE        (0x08)
#define AVB_ACMP_STATUS_STATE_UNAVAILABLE         (0x09)
#define AVB_ACMP_STATUS_NOT_CONNECTED             (0x0a)
#define AVB_ACMP_STATUS_NO_SUCH_CONNECTION        (0x0b)
#define AVB_ACMP_STATUS_COULD_NOT_SEND_MESSAGE    (0x0c)
#define AVB_ACMP_STATUS_TALKER_MISBEHAVING        (0x0d)
#define AVB_ACMP_STATUS_LISTENER_MISBEHAVING      (0x0e)
#define AVB_ACMP_STATUS_RFU                       (0x0f)
#define AVB_ACMP_STATUS_CONTROLLER_NOT_AUTHORIZED (0x10)
#define AVB_ACMP_STATUS_INCOMPATIBLE_REQUEST      (0x11)
#define AVB_ACMP_STATUS_NOT_SUPPORTED             (0x1f)

#define AVB_AVTP_AAF_HDR_GET_SV(hdr)		((hdr->h.f.b1.sv & 0x80) >> 7)
#define AVB_AVTP_AAF_HDR_SET_SV(hdr, val)	(hdr->h.f.b1.sv = (hdr->h.f.b1.sv | ((val << 7) & 0x80)))
#define AVB_AVTP_AAF_HDR_GET_VER(hdr)		((hd->h.f.b1.version & 0x70) >> 4)
#define AVB_AVTP_AAF_HDR_SET_VER(hdr, val)	(hdr->h.f.b1.version = (hdr->h.f.b1.version | ((val << 4)& 0x70)))
#define AVB_AVTP_AAF_HDR_GET_MR(hdr)		((hd->h.f.b1.mr & 0x08) >> 3)
#define AVB_AVTP_AAF_HDR_SET_MR(hdr, val)	(hdr->h.f.b1.mr = (hdr->h.f.b1.mr | ((val << 3) & 0x08)))
#define AVB_AVTP_AAF_HDR_GET_TSV(hdr)		(hd->h.f.b1.tsValid & 0x01)
#define AVB_AVTP_AAF_HDR_SET_TSV(hdr, val)	(hdr->h.f.b1.tsValid = (hdr->h.f.b1.tsValid | (val & 0x01)))
#define AVB_AVTP_AAF_HDR_GET_TU(hdr)		(hd->h.f.b2.tu & 0x01)
#define AVB_AVTP_AAF_HDR_SET_TU(hdr, val)	(hdr->h.f.b2.tu = (hdr->h.f.b2.tu | (val & 0x01)))
#define AVB_AVTP_AAF_HDR_GET_NSR(hdr)		((hd->h.f.fsd1.nsr & 0xF0) >> 4)
#define AVB_AVTP_AAF_HDR_SET_NSR(hdr, val)	(hdr->h.f.fsd1.nsr = (hdr->h.f.fsd1.nsr | ((val << 4)& 0xF0)))
#define AVB_AVTP_AAF_HDR_GET_CPF(hdr)		(hd->h.f.fsd1.cpf & 0x03)
#define AVB_AVTP_AAF_HDR_SET_CPF(hdr, val)	(hdr->h.f.fsd1.cpf = (hdr->h.f.fsd1.cpf | (val & 0x03)))
#define AVB_AVTP_AAF_HDR_GET_SP(hdr)		((hd->h.f.fsd2.sp & 0x10) >> 4)
#define AVB_AVTP_AAF_HDR_SET_SP(hdr, val)	(hdr->h.f.fsd2.sp = (hdr->h.f.fsd2.sp | ((val << 4) & 0x10)))
#define AVB_AVTP_AAF_HDR_GET_EVT(hdr)		(hd->h.f.fsd2.evt & 0x0F)
#define AVB_AVTP_AAF_HDR_SET_EVT(hdr, val)	(hdr->h.f.fsd2.evt = (hdr->h.f.fsd2.evt | (val & 0x0F)))

#define AVB_AVTPDU_CTRL_HDR_GET_SV(hdr)			((hdr->h.f.b1.sv & 0x80) >> 7)
#define AVB_AVTPDU_CTRL_HDR_SET_SV(hdr, val)		(hdr->h.f.b1.sv = (hdr->h.f.b1.sv | ((val << 7) & 0x80)))
#define AVB_AVTPDU_CTRL_HDR_GET_VER(hdr)		((hd->h.f.b1.version & 0x70) >> 4)
#define AVB_AVTPDU_CTRL_HDR_SET_VER(hdr, val)		(hdr->h.f.b1.version = (hdr->h.f.b1.version | ((val << 4) & 0x70)))
#define AVB_AVTPDU_CTRL_HDR_GET_MSGTYPE(hdr)		((hd->h.f.b1.msgType & 0x0f))
#define AVB_AVTPDU_CTRL_HDR_SET_MSGTYPE(hdr, val)	(hdr->h.f.b1.msgType = (hdr->h.f.b1.msgType | ((val) & 0x0f)))
#define AVB_AVTPDU_CTRL_HDR_GET_VALIDTIME(hdr)		((hd->h.f.b2.validTime & 0xf8) >> 3)
#define AVB_AVTPDU_CTRL_HDR_SET_VALIDTIME(hdr, val)	(hdr->h.f.b2.validTime = (hdr->h.f.b2.validTime | ((val << 3) & 0xf8)))
#define AVB_AVTPDU_CTRL_HDR_GET_DATALEN(hdr)		((((hdr->h.f.b2.dataLen & 0x0007) << 8) & 0xff00) | (hdr->h.f.dataLen & 0x00ff))
#define AVB_AVTPDU_CTRL_HDR_SET_DATALEN(hdr, val)	((hdr->h.f.b2.dataLen = (hdr->h.f.b2.dataLen | ((val >> 8) & 0x0007))), (hdr->h.f.dataLen = (val & 0x00ff)))

//typedef signed long long int s64;
//typedef signed int s32;
//typedef signed short int s16;
//typedef signed char s8;
//typedef unsigned long long int u64;
//typedef unsigned int u32;
//typedef unsigned short int u16;
//typedef unsigned char u8;

#pragma pack(push, 1)

struct avtPduAafPcmHdr {
	union th {
		struct tf {
			u8 subType;
			union tb1 {
				u8 sv;		/* 1 bit stream valid indication */
				u8 version;	/* 3 bits version */
				u8 mr;		/* 1 bit media clock restart */
				u8 rsv;		/* 2 bits reserved */
				u8 tsValid;	/* 1 bit timestamp valid */
			} b1;
			u8 seqNo;
			union tb2 {
				u8 rsv;		/* 7 bit reserved data */
				u8 tu;		/* 1 bit timestamp uncertain */	
			} b2;
			u64 streamId;
			u32 avtpTS;
			u8 format;
			union tfsd1 {
				u8 nsr;		/* 4 bits nominal sample rate */
				u8 rsv;         /* 2 bits reserved data */
				u8 cpf;         /* first 2 bits of channels per frame */
			} fsd1;
			u8 cpf;                 /* last 8 bits of channels per frame */
			u8 bitDepth;
			u16 streamDataLen;
			union tfsd2 {
				u8 rsv;		/* 3 bits reserved data */
				u8 sp;		/* 1 bit sparse timestamp */
				u8 evt;		/* 4 bits event data */
			} fsd2;
			u8 rsv;
		} f;
		u8 bytes[AVTP_PDU_COMMON_STREAM_HEADER_LENGTH];
	} h;
};

struct avtPduControlHdr {
	union tch {
		struct tcf {
			u8 subType;
			union tcb1 {
				u8 sv;		/* 1 bit stream valid indication */
				u8 version;	/* 3 bits version */
				u8 msgType;	/* 4 bit ControlData/MessageType */
			} b1;
			union tcb2 {
				u8 validTime;	/* 5 bit Status/ValidTime */
				u8 dataLen;	/* First 3 bits of control data length */	
			} b2;
			u8 dataLen;             /* Last 8 bits of control data length */
			u8 streamId[8];    	/* Stream or entity id */
		} f;
		u8 bytes[AVTP_PDU_COMMON_CONTROL_HEADER_LENGTH];
	} h;
};


struct maapPdu {
	union mtch {
		struct mtcf {
			u8 subType;
			union mtcb1 {
				u8 sv;		/* 1 bit stream valid indication */
				u8 version;	/* 3 bits version */
				u8 msgType;	/* 4 bit ControlData/MessageType */
			} b1;
			union mtcb2 {
				u8 maapVersion;	/* 5 bit Status/ValidTime */
				u8 dataLen;	/* First 3 bits of control data length */	
			} b2;
			u8 dataLen;             /* Last 8 bits of control data length */
			u8 streamId[8];    	/* Stream or entity id */
		} f;
		u8 bytes[AVTP_PDU_COMMON_CONTROL_HEADER_LENGTH];
	} h;
	u8 reqMAC[6];
	u16 reqCount;
	u8 conflictMAC[6];
	u16 conflictCount;
};

struct avtpStreamFormat {
	u8 subType;
	union sfb1 {
		u8 res1;        /* 4 bit reserved */
		u8 nsr;	        /* 4 bit Nominal sample rate */
	} b1;
	u8 format;
	u8 bitDepth;
	u8 cpf;                 /* First 8 bits of channels per frame */
	union sfb5 {
		u8 cpf;	        /* Last 2 bits of channels per frame */
		u8 spf;	        /* First 6 bits of samples per frame */	
	} b5;
	union sfb6 {
		u8 spf;	        /* Last 4 bits of samples per frame */
		u8 res2;	/* First 4 bits of reserved */	
	} b6;
	u8 res2;    		/* Last 8 bits of reserved */
};

struct iecStreamFormat {
	u8 subType;
	union isfb1 {
		u8 sf;          /* 1 bit stream format */
		u8 fmt;         /* 6 bit fomrat */
		u8 r;           /* 1 bit reserved */
	} b1;
	union isfb2 {
		u8 fdf_evt;     /* 5 bits */
		u8 fdf_sfc;	/* 3 bits */
	} b2;
	u8 dbs;
	union isfb4 {
		u8 b;		/* 1 bit */
		u8 nb;		/* 1 bit */
		u8 res;		/* 6 bits */
	} b4;
	u8 label_iec_60958_cnt;
	u8 label_mbla_cnt;
	union isfb7 {
		u8 label_midi_cnt;	/* 4 bits */
		u8 label_smptecnt;	/* 4 bits */
	} b7;
};

struct streamFormat {
	union fmt {
		struct avtpStreamFormat avtp;
		struct iecStreamFormat iec;
	} fmt;
};

struct acmPdu {
	u8 ctrlEntityId[8];
	u8 talkerEntityId[8];
	u8 listenerEntityId[8];
	u16 talkerUniqueId;
	u16 listenerUniqueId;
	u8 streamDestMAC[6];
	u16 connectionCount;
	u16 sequenceId;
	u16 flags;
	u16 streamVlanId;
	u16 res;
};

struct aemCmd {
	u8 ctrlEntityId[8];
	u16 seqId;
	u16 cmdType;
};

struct acquireEntCmd {
	struct aemCmd hdr;
	u32 flags;
	u8 ownerId[8];
	u16 descType;
	u16 descIdx;
};

struct readDescpCmd {
	struct aemCmd hdr;
	u16 cfgIdx;
	u16 res;
	u16 descType;
	u16 descIdx;
};

struct setStreamFormatCmd {
	struct aemCmd hdr;
	u16 descType;
	u16 descIdx;
	struct streamFormat fmt;
};

struct readDescpRes {
	struct aemCmd hdr;
	u16 cfgIdx;
	u16 res;
};

struct entityDescp {
	u16 descType;
	u16 descIdx;
	u8 entityId[8];
	u8 entityModelId[8];
	u32 entityCaps;
	u16 talkerStreamSources;
	u16 talkerCaps;
	u16 listenerStreamSinks;
	u16 listenerCaps;
	u32 controlCaps;
	u32 avaiIdx;
	u8 associationId[8];
	u8 entityName[64];
	u16 vendorNameString;
	u16 modelNameString;
	u8 firmwareVer[64];
	u8 groupName[64];
	u8 serialNumber[64];
	u16 cfgCount;
	u16 currCfg;
};

struct configDescpCount {
	u16 descType;
	u16 descCount;
};

struct configDescp {
	u16 descType;
	u16 descIdx;
	u8 objName[64];
	u16 localizedDescp;
	u16 descpCount;
	u16 descpOff;
	struct configDescpCount descps[AVB_AEM_MAX_DESCP_COUNT];
};

struct audioUnitDescp {
	u16 descType;
	u16 descIdx;
	u8 objName[64];
	u16 localizedDescp;
	u16 clockDomainIdx;
	u16 numStreamIp;
	u16 baseStreamIp;
	u16 numStreamOp;
	u16 baseStreamOp;
	u16 numExtIp;
	u16 baseExtIp;
	u16 numExtOp;
	u16 baseExtOp;
	u16 numIntIp;
	u16 baseIntIp;
	u16 numIntOp;
	u16 baseIntOp;
	u16 numControls;
	u16 baseControl;
	u16 numSignalSelector;
	u16 baseSignalSelector;
	u16 numMixers;
	u16 baseMixer;
	u16 numMatrices;
	u16 baseMatrix;
	u16 numSplitters;
	u16 baseSplitter;
	u16 numCombiners;
	u16 baseCombiner;
	u16 numdeMultiplexer;
	u16 basedeMultiplexer;
	u16 numMultiplxer;
	u16 baseMultiplexer;
	u16 numTranscoders;
	u16 baseTranscoder;
	u16 numControlBlocks;
	u16 baseControlBlock;
	u32 currentSamplingRate;
	u16 samplingRatesOffset;
	u16 samplingRatesCount;
	u32 samplingRates[6];
};

struct streamPortDescp {
	u16 descType;
	u16 descIdx;
	u16 clockDomainIdx;
	u16 portFlags;
	u16 numControls;
	u16 baseControl;
	u16 numClusters;
	u16 baseCluster;
	u16 numMaps;
	u16 baseMap;
};

struct extPortDescp {
	u16 descType;
	u16 descIdx;
	u16 clockDomainIdx;
	u16 portFlags;
	u16 numControls;
	u16 baseControl;
	u16 signalType;
	u16 signalIdx;
	u16 signalOp;
	u32 blockLatency;
	u16 jackIdx;
};

struct jackDescp {
	u16 descType;
	u16 descIdx;
	u8 objName[64];
	u16 localizedDescp;
	u16 jackFlags;
	u16 jackType;
	u16 numControls;
	u16 baseControl;
};

struct audioClusterDescp {
	u16 descType;
	u16 descIdx;
	u8 objName[64];
	u16 localizedDescp;
	u16 signalType;
	u16 signalIdx;
	u16 signalOp;
	u32 pathLatency;
	u32 blockLatency;
	u16 numChannels;
	u8 format;
};

struct audMapFmt {
	u16 streamIdx;
	u16 streamChannel;
	u16 clusterOffset;
	u16 clusterChannel;
};

struct audioMapDescp {
	u16 descType;
	u16 descIdx;
	u16 mappingOffset;
	u16 numMappings;
	struct audMapFmt map[8];
};

struct clockSourceDescp {
	u16 descType;
	u16 descIdx;
	u8 objName[64];
	u16 localizedDescp;
	u16 clockSourcFlags;
	u16 clockSourceType;
	u8 clockSourceId[8];
	u16 clockSourceLocType;
	u16 clockSourceLocIdx;
};

struct clockDomainDescp {
	u16 descType;
	u16 descIdx;
	u8 objName[64];
	u16 localizedDescp;
	u16 currClockSource;
	u16 clockSourcesOffset;
	u16 clockSourcesCount;
	u16 clockSources[3];
};

struct avbIfDescp {
	u16 descType;
	u16 descIdx;
	u8  ifName[64];
	u16 localizedDescp;
	u8  macAddr[6];
	u16 ifFlags;
	u8 clockIden[8];
	u8 prio1;
	u8 clockClass;
	u16 offScaledLogVar;
	u8 clockAccu;
	u8 prio2;
	u8 domainNo;
	u8 logSyncInt;
	u8 logAnnoInt;
	u8 logPDelayInt;
	u16 portNo;
};

struct localeDescp {
	u16 descType;
	u16 descIdx;
	u8 localeId[64];
	u16 numStrings;
	u16 baseStringsIdx;
};

struct stringsDescp {
	u16 descType;
	u16 descIdx;
	u8 strings[7][64];
};

struct streamDescp {
	u16 descType;
	u16 descIdx;
	u8 objName[64];
	u16 localizedDescp;
	u16 clockDomainIdx;
	u16 streamFlags;
	struct streamFormat currFmt;
	u16 fmtsOff;
	u16 fmtsCount;
	u8 bkpTalker1EntityId[8];
	u16 bkpTalker1UniqueId;
	u8 bkpTalker2EntityId[8];
	u16 bkpTalker2UniqueId;
	u8 bkpTalker3EntityId[8];
	u16 bkpTalker3UniqueId;
	u8 bkdUpTalkerEntityId[8];
	u16 bkdUpTalkerUniqueId;
	u16 avbIfIdx;
	u32 bufSize;
	struct streamFormat suppFmts[AVB_AEM_MAX_SUPP_FORMATS];
};

struct getCountersCmd {
	struct aemCmd hdr;
	u16 descType;
	u16 descIdx;
};

struct countersDescp {
	struct aemCmd hdr;
	u16 descType;
	u16 descIdx;
	u32 countersValid;
	u32 counters[32];
};

struct getStreamInfoCmd {
	struct aemCmd hdr;
	u16 descType;
	u16 descIdx;
};

struct streamInfo {
	struct aemCmd hdr;
	u16 descType;
	u16 descIdx;
	u32 flags;
	struct streamFormat currFmt;
	u8 streamId[8];
	u32 msrpAccuLat;
	u8 streamDestMAC[6];
	u8 msrpFailureCode;
	u8 res1;
	u8 msrpFailureBridgeId[8];
	u16 streamVlanId;
	u16 res2;
};

struct adpdu {
	u8 entityModelId[8];
	u32 entityCaps;
	u16 talkerStreamSources;
	u16 talkerCaps;
	u16 listenerStreamSinks;
	u16 listenerCaps;
	u32 controlCaps;
	u32 avaiIdx;
	u8 gptpGrandMasterId[8];
	u8 gptpDomainNumber;
	u8 res1[3];
	u16 idenCtrlIdx;
	u16 interfaceIdx;
	u8 associationId[8];
	u32 res2;
};

struct domainmsrpfirstvalue {
	u8 srClassId;
	u8 srClassPrio;
	u16 srClassVID;
};

struct listenermsrpfirstvalue {
	u8 streamid[8];
};

struct talkermsrpfirstvalue {
	u8 streamid[8];
	u8 dataframeparams[8];
	u16 maxFrameSize;
	u16 maxintervalframes;
	u8 priorityandrank;
	u32 accumalatedlatency;
};

struct bridgemsrpfirstvalue {
	u8 streamid[8];
	u8 dataframeparams[8];
	u16 maxFrameSize;
	u16 maxintervalframes;
	u8 priorityandrank;
	u32 accumalatedlatency;
	u8 bridgeid[8];
	u8 failurereason; 
};

struct vectorheader {
	u16 numberofvalues;
};

struct domainvectorattribute {
	struct vectorheader hdr;
	struct domainmsrpfirstvalue val;
	u8 vector[2];
};

struct listnervectorattribute {
	struct vectorheader hdr;
	struct listenermsrpfirstvalue val;
	u8 vector[2];
};

struct talkervectorattribute {
	struct vectorheader hdr;
	struct talkermsrpfirstvalue val;
	u8 vector[1];
};

struct domainmrpmsg {
	u8 attributetype;
	u8 attributelen;
	u16 attributelistlen;
	struct domainvectorattribute attibutelist;
	u16 endmarker;
};

struct listnermrpmsg {
	u8 attributetype;
	u8 attributelen;
	u16 attributelistlen;
	struct listnervectorattribute attibutelist;
	u16 endmarker;
};

struct talkermrpmsg {
	u8 attributetype;
	u8 attributelen;
	u16 attributelistlen;
	struct talkervectorattribute attibutelist;
	u16 endmarker;
};

struct domainmsrpdu {
	u8 protocolversion;
	struct domainmrpmsg msg;
	u16 endmarker;
};

struct listnermsrpdu {
	u8 protocolversion;
	struct listnermrpmsg msg;
	u16 endmarker;
};

struct talkermsrpdu {
	u8 protocolversion;
	struct talkermrpmsg msg;
	u16 endmarker;
};

#pragma pack(pop)

struct socketdata {
	int type;
	int ifidx;
	char srcmac[6];
	char destmac[6];
	struct socket* sock;
	struct iovec txiov;
	struct iovec rxiov;
	struct msghdr txMsgHdr;
	struct sockaddr_ll txSockAddress;
	struct msghdr rxMsgHdr;
	struct sockaddr_ll rxSockAddress;
	char txBuf[AVB_MAX_ETH_FRAME_SIZE];
	char rxBuf[AVB_MAX_ETH_FRAME_SIZE];
};

struct avdecc {
	bool initialized;
	u8 acmpTxState;
	u8 acmpRxState;
	u32 adpAvaiIdx;
	u64 lastADPAdvJiffy;
	struct socketdata sd;
};

struct msrp {
	bool initialized;
	bool started;
	int  rxState;
	int  txState;
	struct socketdata sd;
	u8 streamid[8];
}; 

struct streaminfo {
	int sr;
	int st;
	int seqNo;
	int socketCount;
	unsigned long int timerVal;
	unsigned long int lastTimerTs;
	unsigned long int startts;
	snd_pcm_uframes_t hwIdx;
	snd_pcm_uframes_t hwnwIdx;
	snd_pcm_uframes_t numBytesConsumed;
	snd_pcm_uframes_t periodsize;
	snd_pcm_uframes_t pendingTxFrames;
	snd_pcm_uframes_t framesize;
	snd_pcm_uframes_t buffersize;
	snd_pcm_uframes_t fillsize;
	snd_pcm_uframes_t prevHwIdx;
	snd_pcm_uframes_t framecount;
	snd_pcm_uframes_t accumframecount;
	struct snd_pcm_substream* substream;
	unsigned char* tmpbuf;
};

struct avbcard {
	struct socketdata sd;
	struct snd_card *card;
	struct snd_pcm *pcm[1];
	struct streaminfo tx;
	struct streaminfo rx;
};

struct workdata {
	struct delayed_work work;
	int delayedWorkId;
	union delayed_work_data {
		void* data;
		struct msrp* msrp;
		struct avbcard* card;
		struct avdecc* avdecc;
	} dw;
};

#ifdef AVB_USE_HIGH_RES_TIMER
struct avbhrtimer {
	struct hrtimer timer;
	struct avbcard* card;
};
#endif

struct avbdevice {
	int txts[AVB_MAX_TS_SLOTS];
	int rxts[AVB_MAX_TS_SLOTS];
	int txIdx;
	int rxIdx;
	struct msrp msrp;
	struct avdecc avdecc;
	struct avbcard card;
	struct snd_hwdep *hwdep;
#ifdef AVB_USE_HIGH_RES_TIMER
	struct avbhrtimer txTimer;
#else
	struct timer_list txTimer;
#endif	
	struct workdata* avdeccwd;
	struct workdata* msrpwd;
	struct workdata* avtpwd;
	struct workqueue_struct* wq;
};

static void avb_log(int level, char* fmt, ...);
static bool avb_socket_init(struct socketdata* sd, int rxTimeout);

static int avb_get_avtp_aaf_nsr(int sampleRate);
static int avb_get_avtp_aaf_format(int rtformat);
static void avb_avtp_aaf_header_init(char* buf, struct snd_pcm_substream *substream, struct snd_pcm_hw_params *hw_params);

static int avb_playback_open(struct snd_pcm_substream *substream);
static int avb_playback_close(struct snd_pcm_substream *substream);
static int avb_playback_hw_params(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params);
static int avb_playback_hw_free(struct snd_pcm_substream *substream);
static int avb_playback_prepare(struct snd_pcm_substream *substream);
static int avb_playback_trigger(struct snd_pcm_substream *substream, int cmd);
static snd_pcm_uframes_t avb_playback_pointer(struct snd_pcm_substream *substream);
static int avb_playback_copy(struct snd_pcm_substream *substream,
                       int channel, snd_pcm_uframes_t pos,
                       void __user *dst,
                       snd_pcm_uframes_t count);
#ifdef AVB_USE_HIGH_RES_TIMER
enum hrtimer_restart avb_avtp_timer(struct hrtimer* t);
#else
static void avb_avtp_timer(unsigned long arg);
#endif

static int avb_capture_open(struct snd_pcm_substream *substream);
static int avb_capture_close(struct snd_pcm_substream *substream);
static int avb_capture_hw_params(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params);
static int avb_capture_hw_free(struct snd_pcm_substream *substream);
static int avb_capture_prepare(struct snd_pcm_substream *substream);
static int avb_capture_trigger(struct snd_pcm_substream *substream, int cmd);
static snd_pcm_uframes_t avb_capture_pointer(struct snd_pcm_substream *substream);
static int avb_capture_copy(struct snd_pcm_substream *substream,
                       int channel, snd_pcm_uframes_t pos,
                       void __user *dst,
                       snd_pcm_uframes_t count);
static int avb_avtp_listen(struct avbcard* card);

static bool avb_msrp_init(struct msrp* msrp);
static void avb_msrp_evaluateTalkerAdvertisement(struct msrp* msrp);
static void avb_msrp_evaluateListnerAdvertisement(struct msrp* msrp);
static void avb_msrp_domaindeclarations(struct msrp* msrp);
static void avb_msrp_talkerdeclarations(struct msrp* msrp, bool join, int state);
static void avb_msrp_listenerdeclarations(struct msrp* msrp, bool join, int state);
static int avb_msrp_listen(struct msrp* msrp);

static void avbWqFn(struct work_struct *work);

static int avb_pcm_new(struct avbcard *avbc, int device, int substreams);

#ifdef CONFIG_PM_SLEEP

static int avb_suspend(struct device *pdev);
static int avb_resume(struct device *pdev);

#endif

static int avb_probe(struct platform_device *devptr);
static int avb_remove(struct platform_device *devptr);
static void avb_remove_all(void);
static int __init alsa_avb_init(void);
static void __exit alsa_avb_exit(void);


