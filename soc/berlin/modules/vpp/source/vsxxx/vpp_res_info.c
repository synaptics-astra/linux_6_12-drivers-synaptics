// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Synaptics Incorporated
 *
 */

#include "vpp_res_info.h"

static const RESOLUTION_INFO m_resinfo_table[MAX_NUM_RESS] = {
	/* { active_width, active_height, width, height, hfrontporch, hsyncwidth, hbackporch, vfrontporch, vsyncwidth, vbackporch, type, scan, frame_rate, flag_3d, freq, pts_per_cnt_4 } */
	/*  0: RES_NTSC_M              */ {     720,          480,         858,    525,    19,          62,         57,         4,           3,          15,   TYPE_SD,   SCAN_INTERLACED, FRAME_RATE_59P94,    0,  27000,  6006 },
	/*  1: RES_NTSC_J              */ {     720,          480,         858,    525,    19,          62,         57,         4,           3,          15,   TYPE_SD,   SCAN_INTERLACED, FRAME_RATE_59P94,    0,  27000,  6006 },
	/*  2: RES_PAL_M               */ {     720,          480,         858,    525,    19,          62,         57,         4,           3,          15,   TYPE_SD,   SCAN_INTERLACED, FRAME_RATE_59P94,    0,  27000,  6006 },
	/*  3: RES_PAL_BGH             */ {     720,          576,         864,    625,    12,          63,         69,         2,           3,          19,   TYPE_SD,   SCAN_INTERLACED, FRAME_RATE_50,       0,  27000,  7200 },
	/*  4: RES_525I60              */ {     720,          480,         858,    525,    19,          62,         57,         4,           3,          15,   TYPE_SD,   SCAN_INTERLACED, FRAME_RATE_60,       0,  27027,  6000 },
	/*  5: RES_525I5994            */ {     720,          480,         858,    525,    19,          62,         57,         4,           3,          15,   TYPE_SD,   SCAN_INTERLACED, FRAME_RATE_59P94,    0,  27000,  6006 },
	/*  6: RES_625I50              */ {     720,          576,         864,    625,    12,          63,         69,         2,           3,          19,   TYPE_SD,   SCAN_INTERLACED, FRAME_RATE_50,       0,  27000,  7200 },
	/*  7: RES_525P60              */ {     720,          480,         858,    525,    16,          62,         60,         9,           6,          30,   TYPE_SD,   SCAN_PROGRESSIVE, FRAME_RATE_60,      0,  27027,  6000 },
	/*  8: RES_525P5994            */ {     720,          480,         858,    525,    16,          62,         60,         9,           6,          30,   TYPE_SD,   SCAN_PROGRESSIVE, FRAME_RATE_59P94,   0,  27000,  6006 },
	/*  9: RES_625P50              */ {     720,          576,         864,    625,    12,          64,         68,         5,           5,          39,   TYPE_SD,   SCAN_PROGRESSIVE, FRAME_RATE_50,      0,  27000,  7200 },
	/* 10: RES_720P30              */ {    1280,          720,         3300,   750,    1760,        40,         220,        5,           5,          20,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_30,      0,  74250, 12000 },
	/* 11: RES_720P2997            */ {    1280,          720,         3300,   750,    1760,        40,         220,        5,           5,          20,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_29P97,   0,  74176, 12012 },
	/* 12: RES_720P25              */ {    1280,          720,         3960,   750,    2420,        40,         220,        5,           5,          20,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_25,      0,  74250, 14400 },
	/* 13: RES_720P60              */ {    1280,          720,         1650,   750,    110,         40,         220,        5,           5,          20,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_60,      0,  74250,  6000 },
	/* 14: RES_720P5994            */ {    1280,          720,         1650,   750,    110,         40,         220,        5,           5,          20,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_59P94,   0,  74176,  6006 },
	/* 15: RES_720P50              */ {    1280,          720,         1980,   750,    440,         40,         220,        5,           5,          20,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_50,      0,  74250,  7200 },
	/* 16: RES_1080I60             */ {    1920,         1080,         2200,   1125,   88,          44,         148,        2,           5,          15,   TYPE_HD,   SCAN_INTERLACED,  FRAME_RATE_60,      0, 148500,  6000 },
	/* 17: RES_1080I5994           */ {    1920,         1080,         2200,   1125,   88,          44,         148,        2,           5,          15,   TYPE_HD,   SCAN_INTERLACED,  FRAME_RATE_59P94,   0, 148352,  6006 },
	/* 18: RES_1080I50             */ {    1920,         1080,         2640,   1125,   528,         44,         148,        2,           5,          15,   TYPE_HD,   SCAN_INTERLACED,  FRAME_RATE_50,      0, 148500,  7200 },
	/* 19: RES_1080P30             */ {    1920,         1080,         2200,   1125,   88,          44,         148,        4,           5,          36,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_30,      0, 74250,  12000 },
	/* 20: RES_1080P2997           */ {    1920,         1080,         2200,   1125,   88,          44,         148,        4,           5,          36,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_29P97,   0, 74176,  12012 },
	/* 21: RES_1080P25             */ {    1920,         1080,         2640,   1125,   528,         44,         148,        4,           5,          36,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_25,      0, 74250,  14400 },
	/* 22: RES_1080P24             */ {    1920,         1080,         2750,   1125,   638,         44,         148,        4,           5,          36,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_24,      0, 74250,  15000 },
	/* 23: RES_1080P2398           */ {    1920,         1080,         2750,   1125,   638,         44,         148,        4,           5,          36,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_23P98,   0, 74176,  15015 },
	/* 24: RES_1080P60             */ {    1920,         1080,         2200,   1125,   88,          44,         148,        4,           5,          36,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_60,      0, 148500,  6000 },
	/* 25: RES_1080P5994           */ {    1920,         1080,         2200,   1125,   88,          44,         148,        4,           5,          36,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_59P94,   0, 148352,  6006 },
	/* 26: RES_1080P50             */ {    1920,         1080,         2640,   1125,   528,         44,         148,        4,           5,          36,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_50,      0, 148500,  7200 },
	/* 27: RES_LVDS_1080P48        */ {    1920,         1080,         2750,   1125,   638,         44,         148,        4,           5,          36,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_48,      0, 148500,  7500 },
	/* 28: RES_LVDS_1080P50        */ {    1920,         1080,         2200,   1350,   32,          44,         204,        229,         5,          36,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_50,      0, 148500,  7200 },
	/* 29: RES_LVDS_1080P60        */ {    1920,         1080,         2200,   1125,   32,          44,         204,        4,           5,          36,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_60,      0, 148500,  6000 },
	/* 30: RES_LVDS_2160P12        */ {    1920,         1080,         2200,   1406,   32,          44,         204,        235,         5,          86,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_48,      0, 148470,  7500 },
	/* 31: RES_VGA_480P60          */ {    640,           480,         800,    525,    16,          96,         48,         10,          2,          33,   TYPE_SD,   SCAN_PROGRESSIVE, FRAME_RATE_60,      0, 25200,   6000 },
	/* 32: RES_VGA_480P5994        */ {    640,           480,         800,    525,    16,          96,         48,         10,          2,          33,   TYPE_SD,   SCAN_PROGRESSIVE, FRAME_RATE_59P94,   0, 25175,   6006 },
	/* 33: RES_720P50_3D           */ {    1280,          720,         1980,   750,    440,         40,         220,        5,           5,          20,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_100,     1, 148500,  7200 },
	/* 34: RES_720P60_3D           */ {    1280,          720,         1650,   750,    110,         40,         220,        5,           5,          20,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_120,     1, 148500,  6000 },
	/* 35: RES_720P5994_3D         */ {    1280,          720,         1650,   750,    110,         40,         220,        5,           5,          20,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_119P88,  1, 148352,  6006 },
	/* 36: RES_1080P24_3D          */ {    1920,         1080,         2750,   1125,   638,         44,         148,        4,           5,          36,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_48,      1, 148500, 15000 },
	/* 37: RES_1080P2398_3D        */ {    1920,         1080,         2750,   1125,   638,         44,         148,        4,           5,          36,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_47P96,   1, 148352, 15015 },
	/* 38: RES_1080P30_3D          */ {    1920,         1080,         2200,   1125,   88,          44,         148,        4,           5,          36,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_60,      1, 148500, 12000 },
	/* 39: RES_1080P2997_3D        */ {    1920,         1080,         2200,   1125,   88,          44,         148,        4,           5,          36,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_59P94,   1, 148352, 12012 },
	/* 40: RES_1080P25_3D          */ {    1920,         1080,         2640,   1125,   528,         44,         148,        4,           5,          36,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_50,      1, 148500, 14400 },
	/* 41: RES_1080I60_FP          */ {    1920,         2228,         2200,   2250,    88,         44,         148,        2,           5,          15,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_30,      0, 148500,  12000},
	/* 42: RES_1080I5994_FP        */ {    1920,         2228,         2200,   2250,    88,         44,         148,        2,           5,          15,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_30,      0, 148352,  12000},
	/* 43: RES_1080I50_FP          */ {    1920,         2228,         2640,   2250,   528,         44,         148,        2,           5,          15,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_25,      0, 148500,  14400},
	/* 44: RES_LVDS_1920X540P60_3D */ {    1920,          540,         1980,    625,     8,         44,          8,        16,           5,          64,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_120,     1, 148500,  6000 },
	/* 45: RES_LVDS_1920X540P30_3D */ {    1920,          540,         1980,    625,     8,         44,          8,        16,           5,          64,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_60,      1, 74250,  12000 },
	/* 46: RES_LVDS_1920X540P24_3D */ {    1920,          540,         2475,    625,   407,         44,        104,        16,           5,          64,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_48,      1, 74250,  15000 },
	/* 47: RES_LVDS_720P100_3D  */    {    1280,          720,         1980,    750,   440,         40,         220,         5,          5,          20,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_100,     1, 148500,  7200 },
	/* 48: RES_LVDS_720P120_3D  */    {    1280,          720,         1650,    750,   110,         40,         220,         5,          5,          20,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_120,     1, 148500,  6000 },
	/* 49: RES_LVDS_1080P48_3D */     {    1920,         1080,         2750,   1125,   638,         44,         148,        4,           5,          36,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_48,      1, 148500, 15000 },
	/* 50: RES_LVDS_1080P50_3D */     {    1920,         1080,         2200,    1350,   32,         44,         204,        229,         5,          36,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_50,      1, 148500, 14400 },
	/* 51: RES_LVDS_1080P60_3D */     {    1920,         1080,         2200,   1125,   32,          44,         204,          4,         5,          36,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_60,      1, 148500, 12000 },
	/* 52: RES_LVDS_1920x540P100_3D */{    1920,          540,          2200,   675,    32,         44,         204,         94,         5,          36,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_100,     1, 148500,  7200 },
	/* 53: RES_LVDS_1920X540P120_3D */{    1920,          540,          2202,   562,    32,         44,         206,          7,         5,          8,    TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_120,     1, 148500,  6000 },
	/* 54: RES_LVDS_960x1080P100_3D */{     960,         1080,         1100,   1350,   42,          44,         54,         229,         5,          36,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_100,     1, 148500,  7200 },
	/* 55: RES_LVDS_960x1080P120_3D */{     960,         1080,         1100,   1125,   42,          44,         54,           4,         5,          36,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_120,     1, 148500,  6000 },

	/* 56: RES_4Kx2K2398    */        {    3840,         2160,         5500,   2250,   1276,        88,         296,          8,         10,         72,   TYPE_UHD,  SCAN_PROGRESSIVE, FRAME_RATE_23P98,   0, 296703, 15015 },
	/* 57: RES_4Kx2K24    */          {    3840,         2160,         5500,   2250,   1276,        88,         296,          8,         10,         72,   TYPE_UHD,  SCAN_PROGRESSIVE, FRAME_RATE_24,      0, 297000, 15000 },
	/* 58: RES_4Kx2K24_SMPTE */       {    4096,         2160,         5500,   2250,   1020,        88,         296,          8,         10,         72,   TYPE_UHD,  SCAN_PROGRESSIVE, FRAME_RATE_24,      0, 297000, 15000 },
	/* 59: RES_4Kx2K25    */          {    3840,         2160,         5280,   2250,   1056,        88,         296,          8,         10,         72,   TYPE_UHD,  SCAN_PROGRESSIVE, FRAME_RATE_25,      0, 297000, 14400 },
	/* 60: RES_4Kx2K2997    */        {    3840,         2160,         4400,   2250,   176,         88,         296,          8,         10,         72,   TYPE_UHD,  SCAN_PROGRESSIVE, FRAME_RATE_29P97,   0, 296703, 12012 },
	/* 61: RES_4Kx2K30    */          {    3840,         2160,         4400,   2250,   176,         88,         296,          8,         10,         72,   TYPE_UHD,  SCAN_PROGRESSIVE, FRAME_RATE_30,      0, 297000, 12000 },
	/* 62: RES_4Kx2K50    */          {    3840,         2160,         5280,   2250,   1056,        88,         296,          8,         10,         72,   TYPE_UHD,  SCAN_PROGRESSIVE, FRAME_RATE_50,      0, 594000,  7200 },
	/* 63: RES_4Kx2K5994    */        {    3840,         2160,         4400,   2250,   176,         88,         296,          8,         10,         72,   TYPE_UHD,  SCAN_PROGRESSIVE, FRAME_RATE_59P94,   0, 593406,  6006 },
	/* 64: RES_4Kx2K60    */          {    3840,         2160,         4400,   2250,   176,         88,         296,          8,         10,         72,   TYPE_UHD,  SCAN_PROGRESSIVE, FRAME_RATE_60,      0, 594000,  6000 },
	/* 65: RES_4Kx2K30_HDMI*/         {    3840,         2160,         4400,   2250,   176,         88,         296,          8,         10,         72,   TYPE_UHD,  SCAN_PROGRESSIVE, FRAME_RATE_30,      0, 297000, 12000 },
	/* 66: RES_4Kx1K120    */         {    3840,         1080,         4400,   1125,   176,         88,         296,          4,          5,         36,   TYPE_UHD,  SCAN_PROGRESSIVE, FRAME_RATE_120,     0, 594000,  3000 },
	/* 67: RES_720P_4Kx1K120_3D */    {    1280,          720,         1650,   750,    110,         40,         220,          5,          5,         20,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_120,     1, 148500,  6000 },

	/* 68: RES_720P100  */            {    1280,          720,         1980,   750,    440,         40,         220,          5,          5,         20,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_100,     0, 148500,  3600 },
	/* 69: RES_720P11988  */          {    1280,          720,         1650,   750,    110,         40,         220,          5,          5,         20,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_119P88,  0, 148500,  3000 },
	/* 70: RES_720P120  */            {    1280,          720,         1650,   750,    110,         40,         220,          5,          5,         20,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_120,     0, 148500,  3000 },
	/* 71: RES_1080P100 */            {    1920,         1080,         2640,  1125,    528,         44,         148,          4,          5,         36,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_100,     0, 297000,  3600 },
	/* 72: RES_1080P11988 */          {    1920,         1080,         2200,  1125,    88,          44,         148,          4,          5,         36,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_119P88,  0, 297000,  3003 },
	/* 73: RES_1080P120 */            {    1920,         1080,         2200,  1125,    88,          44,         148,          4,          5,         36,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_120,     0, 297000,  3000 },
	/* 74: RES_1080P8991 */            {    1920,         1080,         2200,  1125,    88,          44,         148,          4,          5,         36,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_89P91,     0, 222750,  4004 },
	/* 75: RES_1080P90 */            {     1920,          1080,         2200,  1125,    88,          44,         148,          4,          5,         36,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_90,     0, 222750,  4000 },

	/* 76: RES_4Kx2K2398_SMPTE*/      {    4096,         2160,         5500,   2250,   1020,        88,         296,          8,         10,         72,   TYPE_UHD,  SCAN_PROGRESSIVE, FRAME_RATE_23P98,   0, 296703, 15015 },
	/* 77: RES_4Kx2K25_SMPTE*/        {    4096,         2160,         5280,   2250,    968,        88,         296,          8,         10,         72,   TYPE_UHD,  SCAN_PROGRESSIVE, FRAME_RATE_25,      0, 297000, 14400 },
	/* 78: RES_4Kx2K2997_SMPTE*/      {    4096,         2160,         4400,   2250,     88,        88,         296,          8,         10,         72,   TYPE_UHD,  SCAN_PROGRESSIVE, FRAME_RATE_29P97,   0, 296703, 12012 },
	/* 79: RES_4Kx2K30_SMPTE*/        {    4096,         2160,         4400,   2250,     88,        88,         296,          8,         10,         72,   TYPE_UHD,  SCAN_PROGRESSIVE, FRAME_RATE_30,      0, 297000, 12000 },
	/* 80: RES_4Kx2K50_SMPTE*/        {    4096,         2160,         5280,   2250,    968,        88,         296,          8,         10,         72,   TYPE_UHD,  SCAN_PROGRESSIVE, FRAME_RATE_50,      0, 594000,  7200 },
	/* 81: RES_4Kx2K5994_SMPTE*/      {    4096,         2160,         4400,   2250,     88,        88,         296,          8,         10,         72,   TYPE_UHD,  SCAN_PROGRESSIVE, FRAME_RATE_59P94,   0, 593406,  6006 },
	/* 82: RES_4Kx2K60_SMPTE*/        {    4096,         2160,         4400,   2250,    176,        88,         296,          8,         10,         72,   TYPE_UHD,  SCAN_PROGRESSIVE, FRAME_RATE_60,      0, 594000,  6000 },

	//Timings for HDMITX
	/* 83: RES_4Kx2K50_420*/          {    3840,         2160,         5280,   2250,   1056,        88,         296,          8,         10,         72,   TYPE_UHD,  SCAN_PROGRESSIVE, FRAME_RATE_50,      0, 297000,  7200 },
	/* 84: RES_4Kx2K5994_420*/        {    3840,         2160,         4400,   2250,   176,         88,         296,          8,         10,         72,   TYPE_UHD,  SCAN_PROGRESSIVE, FRAME_RATE_59P94,   0, 296703,  6006 },
	/* 85: RES_4Kx2K60_420*/          {    3840,         2160,         4400,   2250,   176,         88,         296,          8,         10,         72,   TYPE_UHD,  SCAN_PROGRESSIVE, FRAME_RATE_60,      0, 297000,  6000 },
	/* 86: RES_4Kx2K2398_3D*/         {    3840,         2160,         5500,   2250,   1276,        88,         296,          8,         10,         72,   TYPE_UHD,  SCAN_PROGRESSIVE, FRAME_RATE_47P96,   1, 593406, 15015 },
	/* 87: RES_4Kx2K24_3D */          {    3840,         2160,         5500,   2250,   1276,        88,         296,          8,         10,         72,   TYPE_UHD,  SCAN_PROGRESSIVE, FRAME_RATE_48,      1, 594000, 15000 },
	/* 88: RES_4Kx2K25_3D */          {    3840,         2160,         5280,   2250,   1056,        88,         296,          8,         10,         72,   TYPE_UHD,  SCAN_PROGRESSIVE, FRAME_RATE_50,      1, 594000, 14400 },
	/* 89: RES_4Kx2K2997_3D */        {    3840,         2160,         4400,   2250,   176,         88,         296,          8,         10,         72,   TYPE_UHD,  SCAN_PROGRESSIVE, FRAME_RATE_59P94,   1, 593406, 12012 },
	/* 90: RES_4Kx2K30_3D */          {    3840,         2160,         4400,   2250,   176,         88,         296,          8,         10,         72,   TYPE_UHD,  SCAN_PROGRESSIVE, FRAME_RATE_60,      1, 594000, 12000 },

	/* 91: RES_LVDS_1088P60 */        {    1932,         1088,         2200,   1125,     60,        44,         110,          4,         5,          36,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_60,      0, 148500,  6000 },
	/* 92: RES_LVDS_1366x768P60 */    {    1366,          768,         1500,    825,     48,        32,          54,          10,         5,         42,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_60,      0,  74250,  6000 },
	/* 93: RES_LVDS_1366x768P5994 */  {    1366,          768,         1500,    825,     48,        32,          54,          10,         5,         42,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_59P94,   0,  74176,  6006 },
	/* 94: RES_LVDS_1366x768P50 */    {    1366,          768,         1800,    825,     48,        32,         354,          10,         5,         42,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_50,      0,  74250,  7200 },
	/* 95: RES_LVDS_1366x768P48 */    {    1366,          768,         1875,    825,     48,        32,         429,          10,         5,         42,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_48,      0,  74250,  7500 },
	/* 96: RES_LVDS_1366x768P4796 */  {    1366,          768,         1875,    825,     48,        32,         429,          10,         5,         42,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_47P96,   0,  74176,  7507 },
	/* 97. RES_DSI_540x960P60 */      {     540,          960,          825,   1220,     40,         5,         240,         245,         2,         13,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_60,      0,  60667,  6000 },
	/* 98. RES_DSI_1920x1200P60 */    {    1920,         1200,         2044,   1224,     37,        25,          62,           2,         4,         18,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_60,      0, 150000,  6000 },
	/* 99. RES_DSI_800x1280P60 */     {     800,         1280,          882,   1327,     40,        20,          22,          20,         4,         23,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_60,      0,  70220,  6000 },
	/* 100: RES_DSI_1080P5994  */     {    1920,         1080,         2080,   1111,     48,        32,          80,           3,         5,         23,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_59P94,   0, 138500,  6006 },
	/* 101. RES_DSI_WNC_800x1280P60 */{     800,         1280,          950,   1312,     60,        30,          60,          16,         2,         14,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_60,      0,  74784,  6000 },
	/* 102. RES_DSI_CUSTOM */         {    1920,         1080,         2200,   1125,     88,        44,         148,           4,         5,         36,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_60,      0, 148500,  6000 },
	/* 103. RES_DSI_UBQ_800x1280P60 */{     800,         1280,          880,   1326,     40,        20,          20,          30,         4,         12,   TYPE_HD,   SCAN_PROGRESSIVE, FRAME_RATE_60,      0,  70012,  6000 },

	// CVT RB Timings for Monitors
	/* 104. RES_CVT_RB_3840_1600P60 */{    3840,         1600,         4000,   1646,     48,        32,          80,          3,         10,         33,   TYPE_UHD,  SCAN_PROGRESSIVE, FRAME_RATE_60,      0, 395000,  6000 },
	/* 105. RES_CVT_RB_1920_1600P60 */{    1920,         1600,         2080,   1646,     48,        32,          80,          3,         10,         33,   TYPE_UHD,  SCAN_PROGRESSIVE, FRAME_RATE_60,      0, 205250,  6000 },
	/* 106. RES_CVT_RB_3440_1440P60 */{    3440,         1440,         3600,   1481,     48,        32,          80,          3,         10,         28,   TYPE_UHD,  SCAN_PROGRESSIVE, FRAME_RATE_60,      0, 319750,  6000 },
	/* 107. RES_CVT_RB_1720_1440P60 */{    1720,         1440,         1880,   1481,     48,        32,          80,          3,         10,         28,   TYPE_UHD,  SCAN_PROGRESSIVE, FRAME_RATE_60,      0, 167000,  6000 },
	/* 108. RES_CVT_RB_1920_2160P60 */{    1920,         2160,         2080,   2230,     48,        32,          80,          3,         10,         57,   TYPE_UHD,  SCAN_PROGRESSIVE, FRAME_RATE_60,      0, 278300,  6000 },
	/* 109. RES_CVT_RB_1920x1200P60 */{    1920,         1200,         2080,   1235,     48,        32,          80,          3,          6,         26,    TYPE_HD,  SCAN_PROGRESSIVE, FRAME_RATE_60,      0, 154000,  6000 },
	/* 110. RES_CUSTOM_2880x1440P60 */{    2880,         1440,         2953,   1456,     50,         8,          15,          8,          2,          6,   TYPE_UHD,  SCAN_PROGRESSIVE, FRAME_RATE_60,      0, 257970,  6000 },
	/* 111. RES_VESA_800x600P60    */ {     800,          600,         1056,    628,     40,       128,          88,          1,          4,         23,    TYPE_SD,  SCAN_PROGRESSIVE, FRAME_RATE_60,      0,  39790,  6000 },
	/* 112. RES_VESA_1280x768P60   */ {    1280,          768,         1440,    790,     48,        32,          80,          3,          7,         12,    TYPE_HD,  SCAN_PROGRESSIVE, FRAME_RATE_60,      0,  68256,  6000 },
	/* 113. RES_VESA_1360x768P60   */ {    1360,          768,         1792,    795,     64,       112,         256,          3,          6,         18,    TYPE_HD,  SCAN_PROGRESSIVE, FRAME_RATE_60,      0,  85478,  6000 },
	/* 114. RES_VESA_1024x768P60   */ {    1024,          768,         1344,    806,     24,       136,         160,          3,          6,         29,    TYPE_HD,  SCAN_PROGRESSIVE, FRAME_RATE_60,      0,  64995,  6000 },
	/* 115. RES_CUSTOM_1280x800P60 */ {    1280,          800,         1680,    828,     64,       136,         200,          1,          3,         24,    TYPE_HD,  SCAN_PROGRESSIVE, FRAME_RATE_60,      0,  83462,  6000 },
	/* 116. RES_VESA_1280x1024P60  */ {    1280,         1024,         1688,   1066,     48,       112,         248,          1,          3,         38,    TYPE_HD,  SCAN_PROGRESSIVE, FRAME_RATE_60,      0, 107964,  6000 },
	/* 117. RES_VESA_1440X900P60   */ {    1440,          900,         1904,    934,     80,       152,         232,          3,          6,         25,    TYPE_HD,  SCAN_PROGRESSIVE, FRAME_RATE_60,      0, 106700,  6000 },
	/* 118. RES_CVT_1400x1050P60   */ {    1400,         1050,         1864,   1089,     88,       144,         232,          3,          4,         32,    TYPE_HD,  SCAN_PROGRESSIVE, FRAME_RATE_60,      0, 121793,  6000 },
	/* 119. RES_VESA_1680x1050P60  */ {    1680,         1050,         2240,   1089,    104,       176,         280,          3,          6,         30,    TYPE_HD,  SCAN_PROGRESSIVE, FRAME_RATE_60,      0, 146361,  6000 },
	/* 120. RES_VESA_1600x1200P60  */ {    1600,         1200,         2160,   1250,     64,       192,         304,          1,          3,         46,    TYPE_HD,  SCAN_PROGRESSIVE, FRAME_RATE_60,      0, 162000,  6000 },
	/* 121. RES_CUSTOM_1366X768P60 */ {    1366,          768,         1500,    825,     48,        32,          54,         10,          5,         42,    TYPE_HD,  SCAN_PROGRESSIVE, FRAME_RATE_60,      0,  74250,  6000 },
	/* 122. RES_CVT_RB_2560X1440P60 */{    2560,         1440,         2720,   1481,     48,        32,          80,          3,          5,         33,   TYPE_UHD,  SCAN_PROGRESSIVE, FRAME_RATE_60,      0, 241500,  6000 },
	/* RES_RESET   */ {       0,            0,            1,      1,     0,          0,           0,        0,           0,           0,         0,                0, 0,    0 }

};

static int syna_get_fps_map (int fps)
{
	switch (fps) {
		case 24: return FRAME_RATE_24;
		case 25: return FRAME_RATE_25;
		case 30: return FRAME_RATE_30;
		case 50: return FRAME_RATE_50;
		case 60: return FRAME_RATE_60;
		default: return  -1;
	}
}

int syna_get_res_index(int active_width, int active_height, int scan, int freq, int fps)
{
	int i;
	int fps_index;

	fps_index = syna_get_fps_map(fps);
	if (fps_index < 0) {
		return -1;
	}

	for (i = 0; i < MAX_NUM_RESS; i++) {
		if ((active_width == m_resinfo_table[i].active_width) &&
				(active_height == m_resinfo_table[i].active_height) &&
				(scan == m_resinfo_table[i].scan) &&
				(freq == m_resinfo_table[i].freq) &&
				(fps_index == m_resinfo_table[i].frame_rate)) {
			break;
		}
	}

	if (i == MAX_NUM_RESS) {
		return -1;
	}
	return i;
}

RESOLUTION_INFO* syna_get_res_info(int res_index)
{

	RESOLUTION_INFO *res_info = (RESOLUTION_INFO*) NULL;

	if (res_index < MAX_NUM_RESS)
		res_info = (RESOLUTION_INFO*) &m_resinfo_table[res_index];

	return res_info;
}
