/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2012 Joel Holdsworth <joel@airwebreathe.org.uk>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <math.h>
#include <stdbool.h>
#include <glib.h>
#include <glib/gstdio.h>
#include "protocol.h"

/* DSView's hardware/DSL/command.[ch] control transport. */
#define DSLOGIC_CMD_CTL_WR		0xb0
#define DSLOGIC_CMD_CTL_RD_PRE		0xb1
#define DSLOGIC_CMD_CTL_RD		0xb2
#define DSLOGIC_CTL_MAX_DATA		60
#define DSLOGIC_CTL_READ_DELAY_US	(10 * 1000)

enum dslogic_ctl_destination {
	DSLOGIC_CTL_FW_VERSION = 0,
	DSLOGIC_CTL_REVID_VERSION = 1,
	DSLOGIC_CTL_HW_STATUS = 2,
	DSLOGIC_CTL_PROG_B = 3,
	DSLOGIC_CTL_LED = 5,
	DSLOGIC_CTL_INTRDY = 6,
	DSLOGIC_CTL_WORDWIDE = 7,
	DSLOGIC_CTL_START = 8,
	DSLOGIC_CTL_STOP = 9,
	DSLOGIC_CTL_BULK_WR = 10,
	DSLOGIC_CTL_NVM = 12,
	DSLOGIC_CTL_I2C_REG = 14,
	DSLOGIC_CTL_I2C_STATUS = 15,
};

#define DSLOGIC_STATUS_GPIF_DONE	(1 << 7)
#define DSLOGIC_STATUS_FPGA_DONE	(1 << 6)
#define DSLOGIC_STATUS_FPGA_INIT_B	(1 << 5)
#define DSLOGIC_STATUS_SYS_OVERFLOW	(1 << 4)
#define DSLOGIC_STATUS_SYS_CLEAR		(1 << 3)
#define DSLOGIC_WRITE_PROG_B		(1 << 2)
#define DSLOGIC_WRITE_INTRDY		(1 << 7)
#define DSLOGIC_WRITE_WORDWIDE		(1 << 0)
#define DSLOGIC_LED_RED			(1 << 1)
#define DSLOGIC_LED_GREEN		(1 << 0)
#define DSLOGIC_ADDR_VTH			0x78
#define DSLOGIC_ADDR_CONTROL0		0x70
#define DSLOGIC_ADDR_HDL_VERSION	0x04
#define DSLOGIC_ADDR_SEC_CONTROL	0x73
#define DSLOGIC_ADDR_SEC_DATA		0x75
#define DSLOGIC_HDL_VERSION			0x0e
#define DSLOGIC_WRITE_FORCE_RDY		(1 << 1)
#define DSLOGIC_SECURITY_READY		(1 << 3)
#define DSLOGIC_SECURITY_PASS		(1 << 4)
#define DSLOGIC_SECURITY_STEPS		8
#define DSLOGIC_SECURITY_EEPROM		0x3c00
#define DSLOGIC_SECURITY_START		0x0513
#define DSLOGIC_SECURITY_CHECK		0x0219
#define DSLOGIC_SECURITY_RETRIES		8

#define DS_MAX_TRIG_PERCENT		90

#define DS_MODE_TRIG_EN			(1 << 0)
#define DS_MODE_CLK_TYPE		(1 << 1)
#define DS_MODE_CLK_EDGE		(1 << 2)
#define DS_MODE_RLE_MODE		(1 << 3)
#define DS_MODE_HALF_MODE		(1 << 5)
#define DS_MODE_QUAR_MODE		(1 << 6)
#define DS_MODE_FILTER			(1 << 8)
#define DS_MODE_STREAM_MODE		(1 << 12)
#define DS_MODE_INT_TEST		(1 << 15)

#define DSLOGIC_ATOMIC_SAMPLES		(sizeof(uint64_t) * 8)
#define DSLOGIC_ATOMIC_BYTES		sizeof(uint64_t)

/*
 * The FPGA is configured with TLV tuples. Length is specified as the
 * number of 16-bit words.
 */
#define _DS_CFG(variable, wordcnt) ((variable << 8) | wordcnt)
#define DS_CFG_START			0xf5a5f5a5
#define DS_CFG_MODE			_DS_CFG(0, 1)
#define DS_CFG_DIVIDER			_DS_CFG(1, 2)
#define DS_CFG_COUNT			_DS_CFG(3, 2)
#define DS_CFG_TRIG_POS			_DS_CFG(5, 2)
#define DS_CFG_TRIG_GLB			_DS_CFG(7, 1)
#define DS_CFG_DSO_COUNT			_DS_CFG(8, 2)
#define DS_CFG_CH_EN			_DS_CFG(10, 2)
#define DS_CFG_FGAIN			_DS_CFG(12, 1)
#define DS_CFG_TRIG			_DS_CFG(64, 160)
#define DS_CFG_END			0xfa5afa5a

#pragma pack(push, 1)

struct version_info {
	uint8_t major;
	uint8_t minor;
};

struct dslogic_ctl_header {
	uint8_t destination;
	uint16_t offset;
	uint8_t size;
};

struct dslogic_ctl_write {
	struct dslogic_ctl_header header;
	uint8_t data[DSLOGIC_CTL_MAX_DATA];
};

struct fpga_config {
	uint32_t sync;

	uint16_t mode_header;
	uint16_t mode;
	uint16_t divider_header;
	uint16_t divider_l;
	uint16_t divider_h;
	uint16_t count_header;
	uint16_t count_l;
	uint16_t count_h;
	uint16_t trig_pos_header;
	uint16_t trig_pos_l;
	uint16_t trig_pos_h;
	uint16_t trig_glb_header;
	uint16_t trig_glb;
	uint16_t dso_count_header;
	uint16_t dso_count_l;
	uint16_t dso_count_h;
	uint16_t ch_en_header;
	uint16_t ch_en_l;
	uint16_t ch_en_h;
	uint16_t fgain_header;
	uint16_t fgain;

	uint16_t trig_header;
	uint16_t trig_mask0[NUM_TRIGGER_STAGES];
	uint16_t trig_mask1[NUM_TRIGGER_STAGES];
	uint16_t trig_value0[NUM_TRIGGER_STAGES];
	uint16_t trig_value1[NUM_TRIGGER_STAGES];
	uint16_t trig_edge0[NUM_TRIGGER_STAGES];
	uint16_t trig_edge1[NUM_TRIGGER_STAGES];
	uint16_t trig_logic0[NUM_TRIGGER_STAGES];
	uint16_t trig_logic1[NUM_TRIGGER_STAGES];
	uint32_t trig_count[NUM_TRIGGER_STAGES];

	uint32_t end_sync;
};

/* DSView's DSL_setting_ext32 follows DSL_setting for 32-channel devices. */
struct fpga_config_ext32 {
	uint32_t sync;
	uint16_t trig_header;
	uint16_t trig_mask0[NUM_TRIGGER_STAGES];
	uint16_t trig_mask1[NUM_TRIGGER_STAGES];
	uint16_t trig_value0[NUM_TRIGGER_STAGES];
	uint16_t trig_value1[NUM_TRIGGER_STAGES];
	uint16_t trig_edge0[NUM_TRIGGER_STAGES];
	uint16_t trig_edge1[NUM_TRIGGER_STAGES];
	uint16_t align_bytes;
	uint32_t end_sync;
};

#pragma pack(pop)

/*
 * This should be larger than the FPGA bitstream image so that it'll get
 * uploaded in one big operation. There seem to be issues when uploading
 * it in chunks.
 */


#define USB_TIMEOUT (3 * 1000)
#define DSLOGIC_BULK_TIMEOUT (1 * 1000)
/* DSView polls these completion bits until ready. Keep a finite 10 s
 * bound here so slow FPGA configuration cannot block the session forever. */
#define DSLOGIC_STATUS_POLL_LIMIT 1000
#define DSLOGIC_TRIGGER_CHECK_ID UINT32_C(0x55555555)
#define DSLOGIC_USB2_TRIGGER_HEADER_SIZE 512
#define DSLOGIC_USB3_TRIGGER_HEADER_SIZE 1024

static int dslogic_control_write(libusb_device_handle *devhdl,
		uint8_t destination, uint16_t offset, const uint8_t *data, uint8_t size)
{
	struct dslogic_ctl_write command = { 0 };
	int ret;

	if (!devhdl || size > DSLOGIC_CTL_MAX_DATA || (size && !data))
		return SR_ERR_ARG;
	command.header.destination = destination;
	command.header.offset = GUINT16_TO_LE(offset);
	command.header.size = size;
	if (size)
		memcpy(command.data, data, size);
	ret = libusb_control_transfer(devhdl, LIBUSB_REQUEST_TYPE_VENDOR |
			LIBUSB_ENDPOINT_OUT, DSLOGIC_CMD_CTL_WR, 0, 0,
			(unsigned char *)&command, sizeof(command.header) + size,
			USB_TIMEOUT);
	if (ret != (int)(sizeof(command.header) + size)) {
		sr_err("DSLogic control write destination %u failed: %s.", destination,
			ret < 0 ? libusb_error_name(ret) : "short transfer");
		return SR_ERR;
	}

	return SR_OK;
}

static int dslogic_control_read(libusb_device_handle *devhdl,
		uint8_t destination, uint16_t offset, uint8_t *data, uint8_t size)
{
	struct dslogic_ctl_header command = {
		.destination = destination,
		.offset = GUINT16_TO_LE(offset),
		.size = size,
	};
	int ret;

	if (!devhdl || !data || !size)
		return SR_ERR_ARG;
	ret = libusb_control_transfer(devhdl, LIBUSB_REQUEST_TYPE_VENDOR |
			LIBUSB_ENDPOINT_OUT, DSLOGIC_CMD_CTL_RD_PRE, 0, 0,
			(unsigned char *)&command, sizeof(command), USB_TIMEOUT);
	if (ret != sizeof(command)) {
		sr_err("DSLogic control-read preparation for destination %u failed: %s.",
			destination, ret < 0 ? libusb_error_name(ret) : "short transfer");
		return SR_ERR;
	}
	g_usleep(DSLOGIC_CTL_READ_DELAY_US);
	ret = libusb_control_transfer(devhdl, LIBUSB_REQUEST_TYPE_VENDOR |
			LIBUSB_ENDPOINT_IN, DSLOGIC_CMD_CTL_RD, 0, 0, data, size,
			USB_TIMEOUT);
	if (ret != size) {
		sr_err("DSLogic control read from destination %u failed: %s.", destination,
			ret < 0 ? libusb_error_name(ret) : "short transfer");
		return SR_ERR;
	}

	return SR_OK;
}

static int dslogic_wait_hw_status(libusb_device_handle *devhdl, uint8_t mask,
		const char *phase, uint8_t *last_status)
{
	unsigned int attempt;
	uint8_t status = 0;

	for (attempt = 0; attempt < DSLOGIC_STATUS_POLL_LIMIT; attempt++) {
		if (dslogic_control_read(devhdl, DSLOGIC_CTL_HW_STATUS, 0,
				&status, sizeof(status)) != SR_OK) {
			if (last_status)
				*last_status = status;
			return SR_ERR;
		}
		if (last_status)
			*last_status = status;
		if (status & mask)
			return SR_OK;
	}

	sr_err("Timed out waiting for DSLogic hardware status during %s: mask 0x%02x after %u polls; last status 0x%02x.",
		phase ? phase : "unspecified phase", mask, DSLOGIC_STATUS_POLL_LIMIT, status);
	return SR_ERR_TIMEOUT;
}

static int command_get_fw_version(libusb_device_handle *devhdl,
				  struct version_info *vi)
{
	int ret;

	ret = dslogic_control_read(devhdl, DSLOGIC_CTL_FW_VERSION, 0,
		(uint8_t *)vi, sizeof(*vi));
	return ret;
}

static int command_get_revid_version(struct sr_dev_inst *sdi, uint8_t *revid)
{
	struct sr_usb_dev_inst *usb = sdi->conn;
	libusb_device_handle *devhdl = usb->devhdl;
	int ret;

	ret = dslogic_control_read(devhdl, DSLOGIC_CTL_REVID_VERSION, 0, revid, 1);
	return ret;
}

static int command_start_acquisition(const struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb;
	usb = sdi->conn;
	return dslogic_control_write(usb->devhdl, DSLOGIC_CTL_START, 0, NULL, 0);
}

static int command_stop_acquisition(const struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb;
	usb = sdi->conn;
	return dslogic_control_write(usb->devhdl, DSLOGIC_CTL_STOP, 0, NULL, 0);
}

SR_PRIV const char *dslogic_profile_fpga_firmware(
		const struct dslogic_profile *profile, double threshold)
{
	if (!profile)
		return NULL;

	/* DSView selects the original DSLogic 3.3 V bitstream below 1.40 V. */
	if (profile->fpga_firmware_3v3 && profile->fpga_firmware_5v &&
			strcmp(profile->fpga_firmware_3v3,
				profile->fpga_firmware_5v))
		return threshold < 1.40 ? profile->fpga_firmware_3v3 :
			profile->fpga_firmware_5v;

	return profile->fpga_firmware_5v ? profile->fpga_firmware_5v :
		profile->fpga_firmware_3v3;
}

static int default_resource_open(void *data, struct sr_context *ctx,
		struct sr_resource *resource, const char *name)
{
	(void)data;
	return sr_resource_open(ctx, resource, SR_RESOURCE_FIRMWARE, name);
}

static ssize_t default_resource_read(void *data, struct sr_context *ctx,
		struct sr_resource *resource, void *buffer, size_t length)
{
	(void)data;
	return sr_resource_read(ctx, resource, buffer, length);
}

static void default_resource_close(void *data, struct sr_context *ctx,
		struct sr_resource *resource)
{
	(void)data;
	sr_resource_close(ctx, resource);
}

static int default_control_write(void *data, libusb_device_handle *devhdl,
		uint8_t destination, uint16_t offset, const uint8_t *buffer,
		uint8_t length)
{
	(void)data;
	return dslogic_control_write(devhdl, destination, offset, buffer, length);
}

static int default_control_read(void *data, libusb_device_handle *devhdl,
		uint8_t destination, uint16_t offset, uint8_t *buffer, uint8_t length)
{
	(void)data;
	return dslogic_control_read(devhdl, destination, offset, buffer, length);
}

static int default_wait_hw_status(void *data, libusb_device_handle *devhdl,
		uint8_t mask, uint8_t *last_status)
{
	(void)data;
	return dslogic_wait_hw_status(devhdl, mask, "FPGA upload", last_status);
}

static int default_bulk_write(void *data, libusb_device_handle *devhdl,
		const uint8_t *buffer, size_t length, int *transferred)
{
	(void)data;
	return libusb_bulk_transfer(devhdl, 2 | LIBUSB_ENDPOINT_OUT,
		(unsigned char *)buffer, (int)length, transferred, DSLOGIC_BULK_TIMEOUT);
}

static const struct dslogic_fpga_upload_ops default_fpga_upload_ops = {
	.resource_open = default_resource_open,
	.resource_read = default_resource_read,
	.resource_close = default_resource_close,
	.control_write = default_control_write,
	.control_read = default_control_read,
	.wait_hw_status = default_wait_hw_status,
	.bulk_write = default_bulk_write,
};

static int dslogic_register_write_with_ops(const struct sr_usb_dev_inst *usb,
		const struct dslogic_fpga_upload_ops *ops, void *ops_data,
		uint8_t address, uint8_t value)
{
	return ops->control_write(ops_data, usb->devhdl, DSLOGIC_CTL_I2C_REG,
		address, &value, sizeof(value));
}

static int dslogic_register_read_with_ops(const struct sr_usb_dev_inst *usb,
		const struct dslogic_fpga_upload_ops *ops, void *ops_data,
		uint8_t address, uint8_t *value)
{
	return ops->control_read(ops_data, usb->devhdl, DSLOGIC_CTL_I2C_STATUS,
		address, value, sizeof(*value));
}

static int dslogic_security_write_with_ops(const struct sr_usb_dev_inst *usb,
		const struct dslogic_fpga_upload_ops *ops, void *ops_data,
		uint16_t command, uint16_t data)
{
	int ret;

	if ((ret = dslogic_register_write_with_ops(usb, ops, ops_data,
			DSLOGIC_ADDR_SEC_DATA, data & 0xff)) != SR_OK ||
			(ret = dslogic_register_write_with_ops(usb, ops, ops_data,
			DSLOGIC_ADDR_SEC_DATA + 1, data >> 8)) != SR_OK ||
			(ret = dslogic_register_write_with_ops(usb, ops, ops_data,
			DSLOGIC_ADDR_SEC_CONTROL, command & 0xff)) != SR_OK)
		return ret;
	return dslogic_register_write_with_ops(usb, ops, ops_data,
		DSLOGIC_ADDR_SEC_CONTROL + 1, command >> 8);
}

static int dslogic_security_check_with_ops(const struct sr_dev_inst *sdi,
		const struct dslogic_fpga_upload_ops *ops, void *ops_data)
{
	const struct sr_usb_dev_inst *const usb = sdi->conn;
	uint8_t encryption[DSLOGIC_SECURITY_STEPS * 2] = { 0 };
	uint8_t control;
	uint8_t data_low;
	uint8_t data_high;
	unsigned int index;
	unsigned int attempt;
	int ret;

	if ((ret = ops->control_read(ops_data, usb->devhdl, DSLOGIC_CTL_NVM,
			DSLOGIC_SECURITY_EEPROM, encryption, sizeof(encryption))) != SR_OK)
		return ret;
	if ((ret = dslogic_register_write_with_ops(usb, ops, ops_data,
			DSLOGIC_ADDR_SEC_CONTROL, 0)) != SR_OK ||
			(ret = dslogic_register_write_with_ops(usb, ops, ops_data,
			DSLOGIC_ADDR_SEC_CONTROL + 1, 0)) != SR_OK)
		return ret;
	g_usleep(10 * 1000);
	if ((ret = dslogic_register_write_with_ops(usb, ops, ops_data,
			DSLOGIC_ADDR_SEC_CONTROL, 1)) != SR_OK ||
			(ret = dslogic_register_write_with_ops(usb, ops, ops_data,
			DSLOGIC_ADDR_SEC_CONTROL + 1, 0)) != SR_OK)
		return ret;
	if ((ret = dslogic_register_read_with_ops(usb, ops, ops_data,
			DSLOGIC_ADDR_SEC_CONTROL, &control)) != SR_OK)
		return ret;
	if (control & DSLOGIC_SECURITY_PASS)
		return SR_ERR;
	if ((ret = dslogic_security_write_with_ops(usb, ops, ops_data,
			DSLOGIC_SECURITY_START, 0)) != SR_OK)
		return ret;

	for (index = DSLOGIC_SECURITY_STEPS; index-- > 0;) {
		if ((ret = dslogic_register_read_with_ops(usb, ops, ops_data,
				DSLOGIC_ADDR_SEC_CONTROL, &control)) != SR_OK)
			return ret;
		if (control & DSLOGIC_SECURITY_PASS)
			return SR_ERR;
		for (attempt = 0; !(control & DSLOGIC_SECURITY_READY);
				attempt++) {
			if (attempt == DSLOGIC_SECURITY_RETRIES) {
				sr_err("DSLogic security engine did not become ready.");
				return SR_ERR_TIMEOUT;
			}
			if ((ret = dslogic_register_read_with_ops(usb, ops, ops_data,
					DSLOGIC_ADDR_SEC_CONTROL, &control)) != SR_OK)
				return ret;
		}
		if ((ret = dslogic_register_read_with_ops(usb, ops, ops_data,
				DSLOGIC_ADDR_SEC_DATA + 1, &data_high)) != SR_OK ||
				(ret = dslogic_register_read_with_ops(usb, ops, ops_data,
				DSLOGIC_ADDR_SEC_DATA, &data_low)) != SR_OK)
			return ret;
		if (data_low || data_high) {
			sr_err("DSLogic security engine returned unexpected challenge data.");
			return SR_ERR;
		}
		if ((ret = dslogic_security_write_with_ops(usb, ops, ops_data,
				DSLOGIC_SECURITY_CHECK,
				(uint16_t)encryption[index * 2] |
				((uint16_t)encryption[index * 2 + 1] << 8))) != SR_OK)
			return ret;
	}

	return SR_OK;
}

static int dslogic_verify_loaded_fpga_with_ops(const struct sr_dev_inst *sdi,
		const struct dslogic_fpga_upload_ops *ops, void *ops_data)
{
	struct dev_context *const devc = sdi->priv;
	const struct sr_usb_dev_inst *const usb = sdi->conn;
	uint8_t version_data[DSLOGIC_ADDR_HDL_VERSION + 1] = { 0 };
	const uint8_t clear = 0;
	int ret;

	if ((ret = ops->control_write(ops_data, usb->devhdl,
			DSLOGIC_CTL_I2C_REG, DSLOGIC_ADDR_CONTROL0, &clear,
			sizeof(clear))) != SR_OK)
		return ret;
	devc->fpga_state = DSLOGIC_FPGA_UNKNOWN;
	if ((ret = ops->control_read(ops_data, usb->devhdl,
			DSLOGIC_CTL_I2C_STATUS, 0, version_data,
			sizeof(version_data))) != SR_OK)
		return ret;
	if (version_data[DSLOGIC_ADDR_HDL_VERSION] != DSLOGIC_HDL_VERSION) {
		sr_err("DSLogic HDL version is incompatible: device 0x%02x, expected 0x%02x.",
			version_data[DSLOGIC_ADDR_HDL_VERSION], DSLOGIC_HDL_VERSION);
		return SR_ERR;
	}
	if (devc->profile->capabilities & DSLOGIC_CAP_SECURITY) {
		if ((ret = dslogic_security_check_with_ops(sdi, ops, ops_data)) != SR_OK) {
			sr_err("DSLogic security check failed.");
			return ret;
		}
	}

	devc->fpga_state = DSLOGIC_FPGA_VERIFIED;
	devc->fpga_needs_upload = FALSE;
	return SR_OK;
}

SR_PRIV int dslogic_read_hw_status(const struct sr_dev_inst *sdi,
		uint8_t *status)
{
	struct dev_context *devc;
	const struct sr_usb_dev_inst *usb;
	int ret;

	if (!sdi || !sdi->priv || !sdi->conn || !status)
		return SR_ERR_ARG;
	devc = sdi->priv;
	usb = sdi->conn;
	if (!usb->devhdl)
		return SR_ERR_ARG;
	ret = dslogic_control_read(usb->devhdl, DSLOGIC_CTL_HW_STATUS, 0,
		status, sizeof(*status));
	if (ret == SR_OK)
		devc->last_hw_status = *status;
	else
		sr_err("Failed to read DSLogic HW_STATUS before FPGA lifecycle action.");
	return ret;
}

SR_PRIV int dslogic_verify_loaded_fpga(const struct sr_dev_inst *sdi)
{
	const struct sr_usb_dev_inst *usb;

	if (!sdi || !sdi->priv || !sdi->conn)
		return SR_ERR_ARG;
	usb = sdi->conn;
	if (!usb->devhdl)
		return SR_ERR_ARG;
	return dslogic_verify_loaded_fpga_with_ops(sdi, &default_fpga_upload_ops,
		NULL);
}

SR_PRIV int dslogic_prepare_fpga_with_ops(const struct sr_dev_inst *sdi,
		const struct dslogic_fpga_upload_ops *ops, void *ops_data)
{
	struct dev_context *devc;
	const struct sr_usb_dev_inst *usb;
	uint8_t status;
	int ret;

	if (!sdi || !sdi->priv || !sdi->conn || !ops || !ops->control_read ||
			!ops->control_write)
		return SR_ERR_ARG;
	devc = sdi->priv;
	usb = sdi->conn;
	if (!usb->devhdl)
		return SR_ERR_ARG;
	ret = ops->control_read(ops_data, usb->devhdl, DSLOGIC_CTL_HW_STATUS,
		0, &status, sizeof(status));
	if (ret != SR_OK) {
		devc->fpga_state = DSLOGIC_FPGA_RECOVERY_REQUIRED;
		sr_err("Failed to read HW_STATUS; refusing to reset PROG_B.");
		return ret;
	}
	devc->last_hw_status = status;
	if (status & DSLOGIC_STATUS_FPGA_DONE)
		return dslogic_verify_loaded_fpga_with_ops(sdi, ops, ops_data);

	devc->fpga_state = DSLOGIC_FPGA_UNCONFIGURED;
	if ((ret = dslogic_select_and_upload_fpga_with_ops(sdi, ops, ops_data)) != SR_OK)
		return ret;
	return dslogic_verify_loaded_fpga_with_ops(sdi, ops, ops_data);
}

SR_PRIV int dslogic_prepare_fpga(const struct sr_dev_inst *sdi)
{
	return dslogic_prepare_fpga_with_ops(sdi, &default_fpga_upload_ops, NULL);
}

SR_PRIV int dslogic_recover_device_state_with_ops(const struct sr_dev_inst *sdi,
		const struct dslogic_fpga_upload_ops *ops, void *ops_data)
{
	struct dev_context *devc;
	const struct sr_usb_dev_inst *usb;
	const uint8_t force_ready = DSLOGIC_WRITE_FORCE_RDY;
	const uint8_t intrdy = DSLOGIC_WRITE_INTRDY;
	const uint8_t wordwide = DSLOGIC_WRITE_WORDWIDE;
	uint8_t status = 0;
	int first_error = SR_OK;
	int ret;

	if (!sdi || !sdi->priv || !sdi->conn || !ops || !ops->control_write ||
			!ops->control_read)
		return SR_ERR_ARG;
	devc = sdi->priv;
	usb = sdi->conn;
	if (!usb->devhdl)
		return SR_ERR_ARG;

	ret = ops->control_write(ops_data, usb->devhdl, DSLOGIC_CTL_I2C_REG,
		DSLOGIC_ADDR_CONTROL0, &force_ready, sizeof(force_ready));
	if (ret != SR_OK)
		first_error = ret;
	ret = ops->control_write(ops_data, usb->devhdl, DSLOGIC_CTL_STOP, 0,
		NULL, 0);
	if (ret != SR_OK && first_error == SR_OK)
		first_error = ret;
	ret = ops->control_read(ops_data, usb->devhdl, DSLOGIC_CTL_HW_STATUS, 0,
		&status, sizeof(status));
	if (ret == SR_OK)
		devc->last_hw_status = status;
	else if (first_error == SR_OK)
		first_error = ret;
	ret = ops->control_write(ops_data, usb->devhdl, DSLOGIC_CTL_INTRDY, 0,
		&intrdy, sizeof(intrdy));
	if (ret != SR_OK && first_error == SR_OK)
		first_error = ret;
	ret = ops->control_write(ops_data, usb->devhdl, DSLOGIC_CTL_WORDWIDE, 0,
		&wordwide, sizeof(wordwide));
	if (ret != SR_OK && first_error == SR_OK)
		first_error = ret;

	if (first_error != SR_OK) {
		devc->fpga_state = DSLOGIC_FPGA_RECOVERY_REQUIRED;
		sr_err("DSLogic hardware recovery failed; physically reconnect the device before reuse.");
		return first_error;
	}
	devc->fpga_state = DSLOGIC_FPGA_UNKNOWN;
	sr_info("DSLogic hardware recovery completed; FPGA state must be checked before reuse.");
	return SR_OK;
}

SR_PRIV int dslogic_recover_device_state(const struct sr_dev_inst *sdi)
{
	return dslogic_recover_device_state_with_ops(sdi, &default_fpga_upload_ops,
		NULL);
}

SR_PRIV int dslogic_select_and_upload_fpga_with_ops(
		const struct sr_dev_inst *sdi,
		const struct dslogic_fpga_upload_ops *ops, void *ops_data)
{
	const char *name = NULL;
	struct sr_resource bitstream;
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	uint8_t *buf = NULL;
	size_t firmware_size;
	ssize_t read_size;
	size_t offset;
	int transferred;
	int result, ret;
	uint8_t command_data[3];
	uint8_t one_byte;
	uint8_t last_status = 0;
	gboolean hardware_touched = FALSE;

	if (!sdi || !ops || !ops->resource_open || !ops->resource_read ||
			!ops->resource_close || !ops->control_write || !ops->control_read ||
			!ops->wait_hw_status || !ops->bulk_write)
		return SR_ERR_ARG;

	drvc = sdi->driver->context;
	devc = sdi->priv;
	usb = sdi->conn;

	name = dslogic_profile_fpga_firmware(devc->profile, devc->cur_threshold);
	if (!name) {
		sr_err("Failed to select FPGA firmware.");
		return SR_ERR;
	}

	sr_dbg("Uploading FPGA firmware '%s'.", name);

	result = ops->resource_open(ops_data, drvc->sr_ctx, &bitstream, name);
	if (result != SR_OK)
		return result;
	if (!bitstream.size || bitstream.size > G_MAXSIZE || bitstream.size > 0xffffff) {
		sr_err("Invalid DSLogic FPGA resource size.");
		result = SR_ERR_DATA;
		goto cleanup_resource;
	}
	firmware_size = (size_t)bitstream.size;
	buf = g_try_malloc(firmware_size);
	if (!buf) {
		result = SR_ERR_MALLOC;
		goto cleanup_resource;
	}
	for (offset = 0; offset < firmware_size; ) {
		read_size = ops->resource_read(ops_data, drvc->sr_ctx, &bitstream,
				buf + offset, firmware_size - offset);
		if (read_size <= 0) {
			result = SR_ERR_DATA;
			goto cleanup;
		}
		offset += (size_t)read_size;
	}

	/* DSView's FPGA sequence: reset PROG_B, wait INIT_B, then bulk-write. */
	hardware_touched = TRUE;
	one_byte = (uint8_t)~DSLOGIC_WRITE_PROG_B;
	if ((result = ops->control_write(ops_data, usb->devhdl,
			DSLOGIC_CTL_PROG_B, 0, &one_byte, 1)) != SR_OK)
		goto cleanup;
	one_byte = (uint8_t)(~DSLOGIC_LED_GREEN & ~DSLOGIC_LED_RED);
	if ((result = ops->control_write(ops_data, usb->devhdl,
			DSLOGIC_CTL_LED, 0, &one_byte, 1)) != SR_OK)
		goto cleanup;
	one_byte = DSLOGIC_WRITE_PROG_B;
	if ((result = ops->control_write(ops_data, usb->devhdl,
			DSLOGIC_CTL_PROG_B, 0, &one_byte, 1)) != SR_OK)
		goto cleanup;
	if ((result = ops->wait_hw_status(ops_data, usb->devhdl,
			DSLOGIC_STATUS_FPGA_INIT_B, &last_status)) != SR_OK) {
		devc->last_hw_status = last_status;
		goto cleanup;
	}

	one_byte = (uint8_t)~DSLOGIC_WRITE_INTRDY;
	if ((result = ops->control_write(ops_data, usb->devhdl,
			DSLOGIC_CTL_INTRDY, 0, &one_byte, 1)) != SR_OK)
		goto cleanup;
	command_data[0] = (uint8_t)firmware_size;
	command_data[1] = (uint8_t)(firmware_size >> 8);
	command_data[2] = (uint8_t)(firmware_size >> 16);
	if ((result = ops->control_write(ops_data, usb->devhdl,
			DSLOGIC_CTL_BULK_WR, 0, command_data, sizeof(command_data))) != SR_OK)
		goto cleanup;
	ret = ops->bulk_write(ops_data, usb->devhdl, buf, firmware_size, &transferred);
	if (ret < 0 || transferred != (int)firmware_size) {
		sr_err("Short or failed DSLogic FPGA transfer: %s.",
			ret < 0 ? libusb_error_name(ret) : "short transfer");
		result = SR_ERR;
		goto cleanup;
	}
	one_byte = DSLOGIC_WRITE_INTRDY;
	if ((result = ops->control_write(ops_data, usb->devhdl,
			DSLOGIC_CTL_INTRDY, 0, &one_byte, 1)) != SR_OK)
		goto cleanup;
	if ((result = ops->wait_hw_status(ops_data, usb->devhdl,
			DSLOGIC_STATUS_GPIF_DONE, &last_status)) != SR_OK) {
		devc->last_hw_status = last_status;
		goto cleanup;
	}
	one_byte = (uint8_t)~DSLOGIC_WRITE_INTRDY;
	if ((result = ops->control_write(ops_data, usb->devhdl,
			DSLOGIC_CTL_INTRDY, 0, &one_byte, 1)) != SR_OK)
		goto cleanup;
	if ((result = ops->wait_hw_status(ops_data, usb->devhdl,
			DSLOGIC_STATUS_FPGA_DONE, &last_status)) != SR_OK) {
		devc->last_hw_status = last_status;
		goto cleanup;
	}
	one_byte = DSLOGIC_LED_GREEN;
	if ((result = ops->control_write(ops_data, usb->devhdl,
			DSLOGIC_CTL_LED, 0, &one_byte, 1)) != SR_OK)
		goto cleanup;
	one_byte = DSLOGIC_WRITE_WORDWIDE;
	result = ops->control_write(ops_data, usb->devhdl,
		DSLOGIC_CTL_WORDWIDE, 0, &one_byte, 1);

cleanup:
	if (result != SR_OK && hardware_touched) {
		ret = dslogic_recover_device_state_with_ops(sdi, ops, ops_data);
		if (ret != SR_OK)
			devc->fpga_state = DSLOGIC_FPGA_RECOVERY_REQUIRED;
	}
	g_free(buf);
cleanup_resource:
	ops->resource_close(ops_data, drvc->sr_ctx, &bitstream);
	if (result == SR_OK)
		devc->fpga_state = DSLOGIC_FPGA_VERIFIED;
	if (result == SR_OK)
		devc->fpga_needs_upload = FALSE;
	return result;
}

static unsigned int enabled_channel_count(const struct sr_dev_inst *sdi)
{
	unsigned int count = 0;
	for (const GSList *l = sdi->channels; l; l = l->next) {
		const struct sr_channel *const probe = (struct sr_channel *)l->data;
		if (probe->enabled)
			count++;
	}
	return count;
}

static unsigned int logic_unitsize(uint32_t channel_mask)
{
	unsigned int highest = 0;

	if (!channel_mask)
		return 0;
	while (channel_mask >>= 1)
		highest++;
	if (highest < 8)
		return 1;
	if (highest < 16)
		return 2;
	return 4;
}

/* Convert channel-round-robin 64-bit atoms to standard packed samples. */
SR_PRIV int dslogic_cross_to_packed(const uint8_t *cross, size_t length,
		uint32_t channel_mask, uint8_t *packed, size_t packed_size,
		size_t *sample_count)
{
	unsigned int channel_count = 0;
	size_t block_size;
	unsigned int unitsize;
	size_t blocks;
	size_t samples;
	size_t block, bit;
	unsigned int channel;

	for (channel = 0; channel < 32; channel++)
		if (channel_mask & (UINT32_C(1) << channel))
			channel_count++;
	block_size = channel_count * sizeof(uint64_t);
	unitsize = logic_unitsize(channel_mask);
	blocks = block_size ? length / block_size : 0;
	samples = blocks * DSLOGIC_ATOMIC_SAMPLES;

	if (!cross || !packed || !sample_count || !channel_mask ||
			length % block_size || packed_size < samples * unitsize)
		return SR_ERR_ARG;

	memset(packed, 0, samples * unitsize);
	for (block = 0; block < blocks; block++) {
		const uint8_t *block_ptr = cross + block * block_size;
		for (channel = 0; channel < 32; channel++) {
			uint64_t word;
			if (!(channel_mask & (UINT32_C(1) << channel)))
				continue;
			memcpy(&word, block_ptr, sizeof(word));
			word = GUINT64_FROM_LE(word);
			block_ptr += sizeof(word);
			for (bit = 0; bit < DSLOGIC_ATOMIC_SAMPLES; bit++) {
				if ((word >> bit) & 1)
					packed[(block * DSLOGIC_ATOMIC_SAMPLES + bit) * unitsize +
						channel / 8] |= (uint8_t)(1U << (channel % 8));
			}
		}
	}
	*sample_count = samples;
	return SR_OK;
}

SR_PRIV int dslogic_cross_remainder_append(uint8_t *remainder,
		size_t *remainder_length, size_t remainder_capacity,
		const uint8_t *input, size_t input_length, uint32_t channel_mask,
		uint8_t **complete, size_t *complete_length)
{
	unsigned int channel_count = 0;
	size_t block_size, total_length, available_length;
	unsigned int channel;
	uint8_t *result;

	if (!remainder || !remainder_length || !input || !complete ||
			!complete_length || !channel_mask ||
			*remainder_length > remainder_capacity ||
			input_length > G_MAXSIZE - *remainder_length)
		return SR_ERR_ARG;
	for (channel = 0; channel < 32; channel++)
		if (channel_mask & (UINT32_C(1) << channel))
			channel_count++;
	block_size = channel_count * DSLOGIC_ATOMIC_BYTES;
	total_length = *remainder_length + input_length;
	available_length = total_length - (total_length % block_size);
	if (total_length - available_length > remainder_capacity)
		return SR_ERR_ARG;

	result = available_length ? g_try_malloc(available_length) : NULL;
	if (available_length && !result)
		return SR_ERR_MALLOC;
	if (available_length) {
		const size_t from_remainder = MIN(*remainder_length, available_length);
		memcpy(result, remainder, from_remainder);
		if (available_length > from_remainder)
			memcpy(result + from_remainder, input,
				available_length - from_remainder);
	}
	if (total_length > available_length) {
		const size_t tail_length = total_length - available_length;
		if (available_length < *remainder_length)
			memmove(remainder, remainder + available_length, tail_length);
		else
			memcpy(remainder, input + available_length - *remainder_length,
				tail_length);
		*remainder_length = tail_length;
	} else {
		*remainder_length = 0;
	}
	*complete = result;
	*complete_length = available_length;
	return SR_OK;
}

static uint32_t enabled_channel_mask(const struct sr_dev_inst *sdi)
{
	uint32_t mask = 0;
	for (const GSList *l = sdi->channels; l; l = l->next) {
		const struct sr_channel *const probe = (struct sr_channel *)l->data;
		if (probe->enabled)
			mask |= 1 << probe->index;
	}
	return mask;
}

/*
 * Get the session trigger and configure the FPGA structure
 * accordingly.
 * @return @c true if any triggers are enabled, @c false otherwise.
 */
static void set_trigger_defaults(struct fpga_config *cfg,
		struct fpga_config_ext32 *cfg32)
{
	unsigned int index;

	for (index = 0; index < NUM_TRIGGER_STAGES; index++) {
		cfg->trig_mask0[index] = 0xffff;
		cfg->trig_mask1[index] = 0xffff;
		cfg->trig_logic0[index] = 2;
		cfg->trig_logic1[index] = 2;
		if (!cfg32)
			continue;
		cfg32->trig_mask0[index] = 0xffff;
		cfg32->trig_mask1[index] = 0xffff;
	}
}

/* DSView's mask/value/edge encoding, sourced from DSL_setting in dsl.c. */
static void set_trigger_match(struct fpga_config *cfg,
		struct fpga_config_ext32 *cfg32, unsigned int stage,
		unsigned int channel, int match)
{
	uint16_t bit = UINT16_C(1) << (channel & 15);
	uint16_t *mask0 = cfg->trig_mask0;
	uint16_t *mask1 = cfg->trig_mask1;
	uint16_t *value0 = cfg->trig_value0;
	uint16_t *value1 = cfg->trig_value1;
	uint16_t *edge0 = cfg->trig_edge0;
	uint16_t *edge1 = cfg->trig_edge1;

	if (channel >= 16 && cfg32) {
		mask0 = cfg32->trig_mask0;
		mask1 = cfg32->trig_mask1;
		value0 = cfg32->trig_value0;
		value1 = cfg32->trig_value1;
		edge0 = cfg32->trig_edge0;
		edge1 = cfg32->trig_edge1;
	}

	switch (match) {
	case SR_TRIGGER_ZERO:
		mask0[stage] &= ~bit;
		mask1[stage] &= ~bit;
		break;
	case SR_TRIGGER_ONE:
		mask0[stage] &= ~bit;
		mask1[stage] &= ~bit;
		value0[stage] |= bit;
		value1[stage] |= bit;
		break;
	case SR_TRIGGER_FALLING:
		mask0[stage] &= ~bit;
		mask1[stage] &= ~bit;
		edge0[stage] |= bit;
		edge1[stage] |= bit;
		break;
	case SR_TRIGGER_RISING:
		mask0[stage] &= ~bit;
		mask1[stage] &= ~bit;
		value0[stage] |= bit;
		value1[stage] |= bit;
		edge0[stage] |= bit;
		edge1[stage] |= bit;
		break;
	case SR_TRIGGER_EDGE:
		/* DSView represents C (either edge) with edge set and mask retained. */
		edge0[stage] |= bit;
		edge1[stage] |= bit;
		break;
	}
}

/* Match DSView's ds_trigger_get_* replication for half/quarter sampling. */
SR_PRIV uint16_t dslogic_transform_trigger_word(uint16_t word,
		gboolean adf4360, gboolean half_samplerate,
		gboolean quarter_samplerate)
{
	uint16_t low;

	if (quarter_samplerate) {
		if (adf4360) {
			low = word & 0xff;
			return low | (low << 8);
		}
		low = word & 0x0f;
		return low | (low << 4) | (low << 8) | (low << 12);
	}
	if (half_samplerate && !adf4360) {
		low = word & 0xff;
		return low | (low << 8);
	}

	return word;
}

static void transform_trigger_words(struct fpga_config *cfg,
		struct fpga_config_ext32 *cfg32, gboolean adf4360,
		gboolean half_samplerate, gboolean quarter_samplerate)
{
	uint16_t *base_fields[] = {
		cfg->trig_mask0, cfg->trig_mask1, cfg->trig_value0,
		cfg->trig_value1, cfg->trig_edge0, cfg->trig_edge1,
	};
	uint16_t *extension_fields[] = {
		cfg32 ? cfg32->trig_mask0 : NULL, cfg32 ? cfg32->trig_mask1 : NULL,
		cfg32 ? cfg32->trig_value0 : NULL, cfg32 ? cfg32->trig_value1 : NULL,
		cfg32 ? cfg32->trig_edge0 : NULL, cfg32 ? cfg32->trig_edge1 : NULL,
	};
	unsigned int field, stage;

	if (!half_samplerate && !quarter_samplerate)
		return;
	for (field = 0; field < G_N_ELEMENTS(base_fields); field++) {
		for (stage = 0; stage < NUM_TRIGGER_STAGES; stage++) {
			base_fields[field][stage] = dslogic_transform_trigger_word(
				base_fields[field][stage], adf4360, half_samplerate,
				quarter_samplerate);
			if (extension_fields[field])
				extension_fields[field][stage] = dslogic_transform_trigger_word(
					extension_fields[field][stage], adf4360, half_samplerate,
					quarter_samplerate);
		}
	}
}

static bool set_trigger(const struct sr_dev_inst *sdi, struct fpga_config *cfg,
		struct fpga_config_ext32 *cfg32)
{
	struct sr_trigger *trigger;
	struct sr_trigger_stage *stage;
	struct sr_trigger_match *match;
	struct dev_context *devc;
	const GSList *l, *m;
	const unsigned int num_enabled_channels = enabled_channel_count(sdi);
	unsigned int num_trigger_stages = 0;
	uint64_t trigger_point;
	uint64_t mem_depth;
	uint64_t max_trigger_point;

	devc = sdi->priv;

	cfg->ch_en_l = enabled_channel_mask(sdi) & 0xffff;
	cfg->ch_en_h = enabled_channel_mask(sdi) >> 16;

	set_trigger_defaults(cfg, cfg32);

	trigger_point = (devc->capture_ratio * devc->actual_limit_samples) / 100;
	if (trigger_point < DSLOGIC_ATOMIC_SAMPLES)
		trigger_point = DSLOGIC_ATOMIC_SAMPLES;
	mem_depth = devc->profile->mem_depth;
	max_trigger_point = dslogic_profile_operation_is_stream(devc->profile,
			devc->operation_mode) ? ((mem_depth * 10) / 100) :
		((mem_depth * DS_MAX_TRIG_PERCENT) / 100);
	if (trigger_point > max_trigger_point)
		trigger_point = max_trigger_point;
	if (trigger_point > UINT32_MAX)
		trigger_point = UINT32_MAX;
	trigger_point &= ~(DSLOGIC_ATOMIC_SAMPLES - 1);
	cfg->trig_pos_l = (uint16_t)(trigger_point & 0xffff);
	cfg->trig_pos_h = (uint16_t)(trigger_point >> 16);

	if (!(trigger = sr_session_trigger_get(sdi->session))) {
		sr_dbg("No session trigger found");
		cfg->trig_glb = (num_enabled_channels & 0x1f) << 8;
		return false;
	}

	for (l = trigger->stages; l && num_trigger_stages < NUM_TRIGGER_STAGES;
			l = l->next, num_trigger_stages++) {
		stage = l->data;
		for (m = stage->matches; m; m = m->next) {
			match = m->data;
			if (!match->channel || !match->channel->enabled ||
					match->channel->index < 0 || match->channel->index >= 32)
				/* Ignore disabled channels with a trigger. */
				continue;
			set_trigger_match(cfg, cfg32, num_trigger_stages,
				match->channel->index, match->match);
		}
	}

	cfg->trig_glb = ((num_enabled_channels & 0x1f) << 8) |
		num_trigger_stages;

	return num_trigger_stages != 0;
}

SR_PRIV size_t dslogic_fpga_config_size(gboolean extension)
{
	return extension ? sizeof(struct fpga_config_ext32) :
		sizeof(struct fpga_config);
}

/* Build the complete logic-only FPGA wire image without touching USB. */
SR_PRIV int dslogic_build_fpga_config(const struct sr_dev_inst *sdi,
		uint8_t *base, size_t base_size, uint8_t *extension,
		size_t extension_size)
{
	const struct dev_context *const devc = sdi->priv;
	struct fpga_config *const cfg = (struct fpga_config *)base;
	struct fpga_config_ext32 *const cfg32 = (struct fpga_config_ext32 *)extension;
	uint16_t mode = 0;
	uint32_t divider, divider_high;
	uint64_t hw_max_samplerate;
	uint8_t pre_div;

	if (!base || !extension || base_size != sizeof(*cfg) ||
			extension_size != sizeof(*cfg32))
		return SR_ERR_ARG;

	memset(cfg, 0, sizeof(*cfg));
	memset(cfg32, 0, sizeof(*cfg32));
	WL32(&cfg->sync, DS_CFG_START);
	WL16(&cfg->mode_header, DS_CFG_MODE);
	WL16(&cfg->divider_header, DS_CFG_DIVIDER);
	WL16(&cfg->count_header, DS_CFG_COUNT);
	WL16(&cfg->trig_pos_header, DS_CFG_TRIG_POS);
	WL16(&cfg->trig_glb_header, DS_CFG_TRIG_GLB);
	WL16(&cfg->dso_count_header, DS_CFG_DSO_COUNT);
	WL16(&cfg->ch_en_header, DS_CFG_CH_EN);
	WL16(&cfg->fgain_header, DS_CFG_FGAIN);
	WL16(&cfg->trig_header, DS_CFG_TRIG);
	WL32(&cfg->end_sync, DS_CFG_END);
	WL32(&cfg32->sync, DS_CFG_START);
	WL16(&cfg32->trig_header, 0x6060);
	WL16(&cfg32->align_bytes, 0xffff);
	WL32(&cfg32->end_sync, DS_CFG_END);

	if (set_trigger(sdi, cfg, (devc->profile->capabilities &
			DSLOGIC_CAP_32_CHANNELS) ? cfg32 : NULL))
		mode |= DS_MODE_TRIG_EN;
	if (devc->operation_mode == DSLOGIC_OPERATION_INTERNAL_TEST)
		mode |= DS_MODE_INT_TEST;
	if (devc->profile->half_samplerate &&
			devc->cur_samplerate == devc->profile->half_samplerate)
		mode |= DS_MODE_HALF_MODE;
	else if (devc->profile->quarter_samplerate &&
			devc->cur_samplerate == devc->profile->quarter_samplerate)
		mode |= DS_MODE_QUAR_MODE;
	transform_trigger_words(cfg, (devc->profile->capabilities &
			DSLOGIC_CAP_32_CHANNELS) ? cfg32 : NULL, devc->profile->adf4360,
		mode & DS_MODE_HALF_MODE, mode & DS_MODE_QUAR_MODE);
	if (dslogic_profile_operation_is_stream(devc->profile,
			devc->operation_mode))
		mode |= DS_MODE_STREAM_MODE;
	if (devc->external_clock) {
		mode |= DS_MODE_CLK_TYPE;
		if (devc->clock_edge == DS_EDGE_FALLING)
			mode |= DS_MODE_CLK_EDGE;
	}
	if (devc->rle_mode)
		mode |= DS_MODE_RLE_MODE;
	if (devc->filter_mode)
		mode |= DS_MODE_FILTER;

	WL16(&cfg->mode, mode);
	hw_max_samplerate = devc->selected_mode->hw_max_samplerate ?
		devc->selected_mode->hw_max_samplerate :
		devc->selected_mode->max_samplerate;
	pre_div = devc->selected_mode->pre_div ?
		devc->selected_mode->pre_div : 1;
	divider = (uint32_t)ceil(hw_max_samplerate * 1.0 /
		devc->cur_samplerate);
	if (!divider)
		return SR_ERR_ARG;
	divider_high = (divider >= pre_div ? pre_div - 1U : divider - 1U) << 8;
	divider = (uint32_t)ceil(divider * 1.0 / pre_div);
	WL16(&cfg->divider_l, divider & 0xffff);
	WL16(&cfg->divider_h, divider_high + (divider >> 16));
	/* The FPGA count is expressed in 16-sample units. */
	WL16(&cfg->count_l, (devc->actual_limit_samples / 16) & 0xffff);
	WL16(&cfg->count_h, (devc->actual_limit_samples / 16) >> 16);

	return SR_OK;
}

SR_PRIV int dslogic_fpga_configure_with_ops(const struct sr_dev_inst *sdi,
		const struct dslogic_fpga_upload_ops *ops, void *ops_data)
{
	struct dev_context *const devc = sdi->priv;
	const struct sr_usb_dev_inst *const usb = sdi->conn;
	uint8_t c[3];
	uint8_t last_status = 0;
	struct fpga_config cfg;
	struct fpga_config_ext32 cfg32;
	int transferred, len, ret, recovery_ret;
	gboolean hardware_touched = FALSE;

	if (!sdi || !devc || !usb || !usb->devhdl || !ops ||
			!ops->control_write || !ops->wait_hw_status || !ops->bulk_write)
		return SR_ERR_ARG;

	sr_dbg("Configuring FPGA.");

	if ((ret = dslogic_build_fpga_config(sdi, (uint8_t *)&cfg, sizeof(cfg),
			(uint8_t *)&cfg32, sizeof(cfg32))) != SR_OK)
		return ret;

	/* Pass in the length of a fixed-size struct. Really. */
	len = sizeof(struct fpga_config) / 2;
	c[0] = len & 0xff;
	c[1] = (len >> 8) & 0xff;
	c[2] = (len >> 16) & 0xff;

	if (devc->profile->usb_speed != LIBUSB_SPEED_SUPER) {
		const uint8_t wordwide = DSLOGIC_WRITE_WORDWIDE;
		hardware_touched = TRUE;
		if ((ret = ops->control_write(ops_data, usb->devhdl,
				DSLOGIC_CTL_WORDWIDE, 0, &wordwide, sizeof(wordwide))) != SR_OK)
			goto recovery;
	}
	hardware_touched = TRUE;
	if ((ret = ops->control_write(ops_data, usb->devhdl, DSLOGIC_CTL_BULK_WR,
			0, c, sizeof(c))) != SR_OK)
		goto recovery;
	if ((ret = ops->wait_hw_status(ops_data, usb->devhdl,
			DSLOGIC_STATUS_SYS_CLEAR, &last_status)) != SR_OK) {
		devc->last_hw_status = last_status;
		goto recovery;
	}

	len = sizeof(struct fpga_config);
	ret = ops->bulk_write(ops_data, usb->devhdl, (const uint8_t *)&cfg,
		len, &transferred);
	if (ret < 0 || transferred != len) {
		sr_err("Failed to send FPGA configuration: %s.", libusb_error_name(ret));
		ret = SR_ERR;
		goto recovery;
	}
	if (devc->profile->capabilities & DSLOGIC_CAP_32_CHANNELS) {
		ret = ops->bulk_write(ops_data, usb->devhdl, (const uint8_t *)&cfg32,
			sizeof(cfg32), &transferred);
		if (ret < 0 || transferred != (int)sizeof(cfg32)) {
			sr_err("Failed to send 32-channel DSLogic FPGA configuration: %s.",
				ret < 0 ? libusb_error_name(ret) : "short transfer");
			ret = SR_ERR;
			goto recovery;
		}
	}
	{
		const uint8_t intrdy = DSLOGIC_WRITE_INTRDY;
		if ((ret = ops->control_write(ops_data, usb->devhdl,
				DSLOGIC_CTL_INTRDY, 0, &intrdy, sizeof(intrdy))) != SR_OK)
			goto recovery;
	}
	if ((ret = ops->wait_hw_status(ops_data, usb->devhdl,
			DSLOGIC_STATUS_GPIF_DONE, &last_status)) != SR_OK) {
		devc->last_hw_status = last_status;
		goto recovery;
	}

	return SR_OK;

recovery:
	if (hardware_touched) {
		recovery_ret = dslogic_recover_device_state_with_ops(sdi, ops, ops_data);
		if (recovery_ret != SR_OK)
			devc->fpga_state = DSLOGIC_FPGA_RECOVERY_REQUIRED;
	}
	return ret;
}

static int fpga_configure(const struct sr_dev_inst *sdi)
{
	return dslogic_fpga_configure_with_ops(sdi, &default_fpga_upload_ops,
		NULL);
}

SR_PRIV int dslogic_set_voltage_threshold(const struct sr_dev_inst *sdi, double threshold)
{
	int ret;
	struct dev_context *const devc = sdi->priv;
	const struct sr_usb_dev_inst *const usb = sdi->conn;
	const uint8_t value = (threshold / 5.0) * 255;

	ret = dslogic_control_write(usb->devhdl, DSLOGIC_CTL_I2C_REG,
		DSLOGIC_ADDR_VTH, &value, sizeof(value));
	if (ret != SR_OK)
		return ret;

	devc->cur_threshold = threshold;

	return SR_OK;
}

SR_PRIV int dslogic_upload_fx2_if_needed(struct sr_context *ctx,
		libusb_device *device, const struct dslogic_profile *profile,
		gboolean firmware_is_loaded, int64_t *firmware_updated)
{
	int ret;

	if (!ctx || !device || !profile || !firmware_updated)
		return SR_ERR_ARG;
	if (firmware_is_loaded)
		return SR_OK;
	if (!dslogic_profile_fx2_firmware(profile)) {
		sr_err("DSLogic profile '%s' has no verified FX2 firmware for cold boot.",
			profile->model);
		return SR_ERR_NA;
	}

	ret = ezusb_upload_firmware(ctx, device, USB_CONFIGURATION,
		dslogic_profile_fx2_firmware(profile));
	if (ret != SR_OK) {
		sr_err("FX2 firmware upload failed for DSLogic profile '%s' (%s).",
			profile->model, dslogic_profile_fx2_firmware(profile));
		return ret;
	}

	/* Do not retain the pre-reset address; dev_open matches by port path. */
	*firmware_updated = g_get_monotonic_time();
	return SR_OK;
}

SR_PRIV int dslogic_dev_open(struct sr_dev_inst *sdi, struct sr_dev_driver *di)
{
	libusb_device **devlist;
	struct sr_usb_dev_inst *usb;
	struct libusb_device_descriptor des;
	struct dev_context *devc;
	struct drv_context *drvc;
	struct version_info vi;
	int ret = SR_ERR, i, device_count;
	uint8_t revid;
	char connection_id[64];

	drvc = di->context;
	devc = sdi->priv;
	usb = sdi->conn;

	device_count = libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
	if (device_count < 0) {
		sr_err("Failed to get device list: %s.",
		       libusb_error_name(device_count));
		return SR_ERR;
	}

	for (i = 0; i < device_count; i++) {
		libusb_get_device_descriptor(devlist[i], &des);

		if (des.idVendor != devc->profile->vid
		    || des.idProduct != devc->profile->pid)
			continue;

		if ((sdi->status == SR_ST_INITIALIZING) ||
				(sdi->status == SR_ST_INACTIVE)) {
			/* Check device by its physical USB bus/port address. */
			if (usb_get_port_path(devlist[i], connection_id, sizeof(connection_id)) < 0)
				continue;

			if (strcmp(sdi->connection_id, connection_id))
				/* This is not the one. */
				continue;
		}

		if (!(ret = libusb_open(devlist[i], &usb->devhdl))) {
			if (usb->address == 0xff)
				/*
				 * First time we touch this device after FW
				 * upload, so we don't know the address yet.
				 */
				usb->address = libusb_get_device_address(devlist[i]);
		} else {
			sr_err("Failed to open device: %s.",
			       libusb_error_name(ret));
			ret = SR_ERR;
			break;
		}

		if (libusb_has_capability(LIBUSB_CAP_SUPPORTS_DETACH_KERNEL_DRIVER)) {
			if (libusb_kernel_driver_active(usb->devhdl, USB_INTERFACE) == 1) {
				if ((ret = libusb_detach_kernel_driver(usb->devhdl, USB_INTERFACE)) < 0) {
					sr_err("Failed to detach kernel driver: %s.",
						libusb_error_name(ret));
					ret = SR_ERR;
					break;
				}
			}
		}

		ret = command_get_fw_version(usb->devhdl, &vi);
		if (ret != SR_OK) {
			sr_err("Failed to get firmware version.");
			break;
		}

		ret = command_get_revid_version(sdi, &revid);
		if (ret != SR_OK) {
			sr_err("Failed to get REVID.");
			break;
		}

		/*
		 * Changes in major version mean incompatible/API changes, so
		 * bail out if we encounter an incompatible version.
		 * Different minor versions are OK, they should be compatible.
		 */
		if (vi.major != DSLOGIC_REQUIRED_VERSION_MAJOR) {
			sr_err("Expected firmware version %d.x, "
			       "got %d.%d.", DSLOGIC_REQUIRED_VERSION_MAJOR,
			       vi.major, vi.minor);
			ret = SR_ERR;
			break;
		}

		sr_info("Opened device on %d.%d (logical) / %s (physical), "
			"interface %d, firmware %d.%d.",
			usb->bus, usb->address, connection_id,
			USB_INTERFACE, vi.major, vi.minor);

		sr_info("Detected REVID=%d, it's a Cypress CY7C68013%s.",
			revid, (revid != 1) ? " (FX2)" : "A (FX2LP)");

		ret = SR_OK;

		break;
	}

	libusb_free_device_list(devlist, 1);
	if (ret != SR_OK && usb->devhdl) {
		libusb_close(usb->devhdl);
		usb->devhdl = NULL;
	}

	return ret;
}

SR_PRIV int dslogic_open_and_claim(struct sr_dev_inst *sdi,
		struct sr_dev_driver *di)
{
	struct sr_usb_dev_inst *usb = sdi->conn;
	int ret;

	ret = dslogic_dev_open(sdi, di);
	if (ret != SR_OK)
		return ret;

	ret = libusb_claim_interface(usb->devhdl, USB_INTERFACE);
	if (ret == 0)
		return SR_OK;

	if (ret == LIBUSB_ERROR_BUSY)
		sr_err("Unable to claim USB interface: it is already claimed.");
	else if (ret == LIBUSB_ERROR_NO_DEVICE)
		sr_err("Unable to claim USB interface: device disconnected.");
	else
		sr_err("Unable to claim USB interface: %s.", libusb_error_name(ret));

	libusb_close(usb->devhdl);
	usb->devhdl = NULL;
	return SR_ERR;
}

SR_PRIV int dslogic_close_and_release(struct sr_dev_inst *sdi)
{
	const struct dev_context *devc;
	struct sr_usb_dev_inst *usb;

	if (!sdi)
		return SR_ERR_ARG;
	usb = sdi->conn;
	devc = sdi->priv;
	if (devc && devc->fpga_state == DSLOGIC_FPGA_RECOVERY_REQUIRED)
		sr_err("DSLogic close is releasing host resources only; physically reconnect the device before reuse.");
	if (!usb || !usb->devhdl)
		return SR_OK;

	libusb_release_interface(usb->devhdl, USB_INTERFACE);
	libusb_close(usb->devhdl);
	usb->devhdl = NULL;
	return SR_OK;
}

SR_PRIV struct dev_context *dslogic_dev_new(void)
{
	struct dev_context *devc;

	devc = g_malloc0(sizeof(struct dev_context));
	devc->profile = NULL;
	devc->fw_updated = 0;
	devc->cur_samplerate = 0;
	devc->limit_samples = 0;
	devc->actual_limit_samples = 0;
	devc->capture_ratio = 0;
	devc->rle_mode = FALSE;
	devc->filter_mode = FALSE;
	devc->operation_mode = DSLOGIC_OPERATION_UNSET;
	devc->selected_mode = NULL;
	devc->clock_edge = DS_EDGE_RISING;
	devc->fpga_state = DSLOGIC_FPGA_UNKNOWN;
	devc->last_hw_status = 0;
	devc->fpga_needs_upload = TRUE;
	devc->threshold_needs_apply = TRUE;
	devc->acquisition_state = DSLOGIC_ACQ_INIT;
	devc->empty_poll_count = 0;
	devc->transfer_completed = FALSE;
	devc->overflow_seen = FALSE;
	devc->cross_remainder_length = 0;
	devc->datafeed_end_sent = FALSE;
	devc->event_source_registered = FALSE;

	return devc;
}

static void abort_acquisition(struct dev_context *devc)
{
	int i;

	devc->acq_aborted = TRUE;

	for (i = devc->num_transfers - 1; i >= 0; i--) {
		if (devc->transfers && devc->transfers[i])
			libusb_cancel_transfer(devc->transfers[i]);
	}
}

SR_PRIV size_t dslogic_sample_count_until_limit(uint64_t limit_samples,
		uint64_t sent_samples, size_t available_samples)
{
	uint64_t remaining;

	if (!limit_samples)
		return available_samples;
	if (sent_samples >= limit_samples)
		return 0;
	remaining = limit_samples - sent_samples;
	return MIN(remaining, (uint64_t)available_samples);
}

SR_PRIV gboolean dslogic_datafeed_end_once(struct dev_context *devc)
{
	if (devc->datafeed_end_sent)
		return FALSE;
	devc->datafeed_end_sent = TRUE;
	return TRUE;
}

SR_PRIV gboolean dslogic_hw_status_has_overflow(uint8_t status)
{
	return (status & DSLOGIC_STATUS_SYS_OVERFLOW) != 0;
}

SR_PRIV gboolean dslogic_trigger_header_starts_data(
		const struct dev_context *devc, uint64_t remain_count)
{
	if (!devc || !devc->profile)
		return FALSE;
	if (dslogic_profile_operation_is_stream(devc->profile,
			devc->operation_mode))
		return TRUE;

	/* Mirrors DSView receive_header() for buffered logic captures. */
	return devc->actual_limit_samples &&
		remain_count < devc->actual_limit_samples;
}

static void send_datafeed_end_once(struct sr_dev_inst *sdi)
{
	if (dslogic_datafeed_end_once(sdi->priv))
		std_session_send_df_end(sdi);
}

static void finish_acquisition(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;

	send_datafeed_end_once(sdi);
	if (devc->event_source_registered) {
		usb_source_remove(sdi->session, devc->ctx);
		devc->event_source_registered = FALSE;
	}

	devc->num_transfers = 0;
	g_free(devc->transfers);
	devc->transfers = NULL;
	g_free(devc->packed_buffer);
	devc->packed_buffer = NULL;
	devc->packed_buffer_size = 0;
	if (devc->cross_remainder_length) {
		sr_err("Incomplete DSLogic atomic block at acquisition end.");
		devc->acquisition_state = DSLOGIC_ACQ_ERROR;
	} else {
		devc->acquisition_state = DSLOGIC_ACQ_FINISH;
	}
	devc->cross_remainder_length = 0;
}

static size_t dslogic_trigger_header_size(const struct dev_context *devc)
{
	return devc->profile->usb_speed == LIBUSB_SPEED_SUPER ?
		DSLOGIC_USB3_TRIGGER_HEADER_SIZE : DSLOGIC_USB2_TRIGGER_HEADER_SIZE;
}

static void free_transfer(struct libusb_transfer *transfer)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	unsigned int i;

	sdi = transfer->user_data;
	devc = sdi->priv;

	g_free(transfer->buffer);
	transfer->buffer = NULL;
	libusb_free_transfer(transfer);

	for (i = 0; i < devc->num_transfers; i++) {
		if (devc->transfers[i] == transfer) {
			devc->transfers[i] = NULL;
			break;
		}
	}

	devc->submitted_transfers--;
	if (devc->submitted_transfers == 0)
		finish_acquisition(sdi);
}

static void resubmit_transfer(struct libusb_transfer *transfer)
{
	struct sr_dev_inst *const sdi = transfer->user_data;
	struct dev_context *const devc = sdi->priv;
	int ret;

	if ((ret = libusb_submit_transfer(transfer)) == LIBUSB_SUCCESS)
		return;

	sr_err("%s: %s", __func__, libusb_error_name(ret));
	devc->acquisition_state = DSLOGIC_ACQ_ERROR;
	abort_acquisition(devc);
	free_transfer(transfer);

}

static void send_data(struct sr_dev_inst *sdi, const uint8_t *data,
	size_t sample_count, unsigned int unitsize)
{
	const struct sr_datafeed_logic logic = {
		.length = sample_count * unitsize,
		.unitsize = unitsize,
		.data = (uint8_t *)data
	};

	const struct sr_datafeed_packet packet = {
		.type = SR_DF_LOGIC,
		.payload = &logic
	};

	sr_session_send(sdi, &packet);
}

static void LIBUSB_CALL receive_transfer(struct libusb_transfer *transfer)
{
	struct sr_dev_inst *const sdi = transfer->user_data;
	struct dev_context *const devc = sdi->priv;
	const uint32_t channel_mask = enabled_channel_mask(sdi);
	size_t cur_sample_count;
	size_t complete_length;
	uint8_t *input;

	gboolean packet_has_error = FALSE;
	size_t num_samples;
	int trigger_offset;

	devc->transfer_completed = TRUE;

	/*
	 * If acquisition has already ended, just free any queued up
	 * transfer that come in.
	 */
	if (devc->acq_aborted) {
		free_transfer(transfer);
		return;
	}
	if (devc->acquisition_state != DSLOGIC_ACQ_DATA &&
			(transfer->status == LIBUSB_TRANSFER_COMPLETED ||
			 transfer->status == LIBUSB_TRANSFER_TIMED_OUT)) {
		/* The trigger header owns the transition to DATA; do not decode an
		 * early data completion as logic payload. */
		if (devc->acquisition_state == DSLOGIC_ACQ_ERROR) {
			free_transfer(transfer);
			return;
		}
		resubmit_transfer(transfer);
		return;
	}

	sr_dbg("receive_transfer(): status %s received %d bytes.",
		libusb_error_name(transfer->status), transfer->actual_length);

	/* Save incoming transfer before reusing the transfer struct. */

	switch (transfer->status) {
	case LIBUSB_TRANSFER_NO_DEVICE:
		abort_acquisition(devc);
		free_transfer(transfer);
		return;
	case LIBUSB_TRANSFER_COMPLETED:
	case LIBUSB_TRANSFER_TIMED_OUT: /* We may have received some data though. */
		break;
	default:
		packet_has_error = TRUE;
		break;
	}

	if (packet_has_error) {
		devc->acquisition_state = DSLOGIC_ACQ_ERROR;
		abort_acquisition(devc);
		free_transfer(transfer);
		return;
	}
	if (transfer->actual_length == 0) {
		devc->empty_transfer_count++;
		if (devc->empty_transfer_count > MAX_EMPTY_TRANSFERS) {
			/*
			 * The FX2 gave up. End the acquisition, the frontend
			 * will work out that the samplecount is short.
			 */
			abort_acquisition(devc);
			free_transfer(transfer);
		} else {
			resubmit_transfer(transfer);
		}
		return;
	} else {
		devc->empty_transfer_count = 0;
	}

	if (dslogic_cross_remainder_append(devc->cross_remainder,
			&devc->cross_remainder_length, sizeof(devc->cross_remainder),
			transfer->buffer, transfer->actual_length, channel_mask,
			&input, &complete_length) != SR_OK) {
		abort_acquisition(devc);
		free_transfer(transfer);
		return;
	}
	if (complete_length) {
		if (dslogic_cross_to_packed(input, complete_length, channel_mask,
				devc->packed_buffer, devc->packed_buffer_size,
				&cur_sample_count) != SR_OK) {
			g_free(input);
			abort_acquisition(devc);
			free_transfer(transfer);
			return;
		}
		g_free(input);
	} else {
		cur_sample_count = 0;
	}

	if (cur_sample_count && (!devc->actual_limit_samples ||
			devc->sent_samples < devc->actual_limit_samples)) {
		num_samples = dslogic_sample_count_until_limit(
			devc->actual_limit_samples, devc->sent_samples, cur_sample_count);

		/**
		 * The DSLogic emits sample data as sequences of 64-bit sample words
		 * in a round-robin i.e. 64-bits from channel 0, 64-bits from channel 1
		 * etc. for each of the enabled channels, then looping back to the
		 * channel.
		 *
		 * Because sigrok's internal representation is bit-interleaved channels
		 * we must recast the data.
		 *
		 * Hopefully in future it will be possible to pass the data on as-is.
		 */
		/* Send the incoming transfer to the session bus. */
		if (devc->trigger_pos > devc->sent_samples
			&& devc->trigger_pos <= devc->sent_samples + num_samples) {
			/* DSLogic trigger in this block. Send trigger position. */
		trigger_offset = devc->trigger_pos - devc->sent_samples;
			/* Pre-trigger samples. */
			send_data(sdi, devc->packed_buffer, trigger_offset, devc->logic_unitsize);
			devc->sent_samples += trigger_offset;
			/* Trigger position. */
			devc->trigger_pos = 0;
			std_session_send_df_trigger(sdi);
			/* Post trigger samples. */
			num_samples -= trigger_offset;
			send_data(sdi, devc->packed_buffer + trigger_offset * devc->logic_unitsize,
				num_samples, devc->logic_unitsize);
			devc->sent_samples += num_samples;
		} else {
			send_data(sdi, devc->packed_buffer, num_samples, devc->logic_unitsize);
			devc->sent_samples += num_samples;
		}
	}

	if (devc->actual_limit_samples &&
			devc->sent_samples >= devc->actual_limit_samples) {
		abort_acquisition(devc);
		free_transfer(transfer);
	} else
		resubmit_transfer(transfer);
}

static int receive_data(int fd, int revents, void *cb_data)
{
	struct timeval tv;
	const struct sr_dev_inst *sdi;
	struct dev_context *devc;
	const struct sr_usb_dev_inst *usb;
	uint8_t hw_status;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	devc = sdi->priv;
	usb = sdi->conn;

	tv.tv_sec = tv.tv_usec = 0;
	libusb_handle_events_timeout(devc->ctx->libusb_ctx, &tv);
	if (devc->transfer_completed)
		devc->empty_poll_count = 0;
	else
		devc->empty_poll_count++;
	/* DSView polls HW_STATUS after 16 quiet event-loop turns in stream mode.
	 * Sigrok has no SR_DF_OVERFLOW packet, so retain the hardware fact in the
	 * standard error/end path rather than introducing a private packet type. */
	if (dslogic_profile_operation_is_stream(devc->profile,
			devc->operation_mode) && devc->empty_poll_count > 16) {
		if (dslogic_control_read(usb->devhdl, DSLOGIC_CTL_HW_STATUS, 0,
				&hw_status, sizeof(hw_status)) == SR_OK &&
				dslogic_hw_status_has_overflow(hw_status)) {
			devc->overflow_seen = TRUE;
			sr_err("DSLogic hardware overflow reported by HW_STATUS.");
		}
		devc->empty_poll_count = 0;
	}
	devc->transfer_completed = FALSE;

	return TRUE;
}

static size_t to_bytes_per_ms(const struct sr_dev_inst *sdi)
{
	const struct dev_context *const devc = sdi->priv;
	const size_t ch_count = enabled_channel_count(sdi);

	if (dslogic_profile_operation_is_stream(devc->profile,
			devc->operation_mode))
		return (devc->cur_samplerate * ch_count) / (1000 * 8);


	/* If we're in buffered mode, the transfer rate is not so important,
	 * but we expect to get at least 10% of the high-speed USB bandwidth.
	 */
	return 35000000 / (1000 * 10);
}

static size_t get_buffer_size(const struct sr_dev_inst *sdi)
{
	/*
	 * The buffer should be large enough to hold 10ms of data and
	 * a multiple of the size of a data atom.
	 */
	const size_t block_size = enabled_channel_count(sdi) * 512;
	const size_t s = 10 * to_bytes_per_ms(sdi);
	if (!block_size)
		return s;
	return ((s + block_size - 1) / block_size) * block_size;
}

static unsigned int get_number_of_transfers(const struct sr_dev_inst *sdi)
{
	/* Total buffer size should be able to hold about 100ms of data. */
	const unsigned int s = get_buffer_size(sdi);
	const unsigned int n = (100 * to_bytes_per_ms(sdi) + s - 1) / s;
	return (n > NUM_SIMUL_TRANSFERS) ? NUM_SIMUL_TRANSFERS : n;
}

static unsigned int get_timeout(const struct sr_dev_inst *sdi)
{
	const size_t total_size = get_buffer_size(sdi) *
		get_number_of_transfers(sdi);
	const unsigned int timeout = total_size / to_bytes_per_ms(sdi);
	return timeout + timeout / 4; /* Leave a headroom of 25% percent. */
}

static void discard_unsubmitted_transfer(struct libusb_transfer *transfer)
{
	if (!transfer)
		return;
	g_free(transfer->buffer);
	libusb_free_transfer(transfer);
}

static void release_unsubmitted_transfers(struct dev_context *devc)
{
	unsigned int index;

	if (devc->transfers) {
		for (index = 0; index < devc->num_transfers; index++) {
			discard_unsubmitted_transfer(devc->transfers[index]);
			devc->transfers[index] = NULL;
		}
	}
	devc->num_transfers = 0;
	g_free(devc->transfers);
	devc->transfers = NULL;
	g_free(devc->packed_buffer);
	devc->packed_buffer = NULL;
	devc->packed_buffer_size = 0;
}

/* Failure before event-source registration must synchronously drain cancels. */
static void cancel_and_drain_transfers(struct sr_dev_inst *sdi)
{
	struct dev_context *const devc = sdi->priv;
	struct timeval tv = { 0, 0 };
	unsigned int attempt;

	abort_acquisition(devc);
	for (attempt = 0; devc->submitted_transfers && attempt < 100; attempt++)
		libusb_handle_events_timeout(devc->ctx->libusb_ctx, &tv);
	if (devc->submitted_transfers)
		sr_err("Failed to drain cancelled DSLogic USB transfers.");
}

static void LIBUSB_CALL trigger_receive(struct libusb_transfer *transfer);

static int start_transfers(const struct sr_dev_inst *sdi)
{
	const size_t channel_count = enabled_channel_count(sdi);
	const uint32_t channel_mask = enabled_channel_mask(sdi);
	const size_t size = get_buffer_size(sdi);
	const unsigned int num_transfers = get_number_of_transfers(sdi);
	const unsigned int timeout = get_timeout(sdi);

	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	struct libusb_transfer *transfer;
	unsigned int i;
	int ret;
	unsigned char *buf;

	devc = sdi->priv;
	usb = sdi->conn;

	devc->sent_samples = 0;
	devc->acq_aborted = FALSE;
	devc->empty_transfer_count = 0;
	devc->empty_poll_count = 0;
	devc->transfer_completed = FALSE;
	devc->overflow_seen = FALSE;
	devc->submitted_transfers = 0;

	g_free(devc->transfers);
	devc->transfers = g_try_malloc0(sizeof(*devc->transfers) * (num_transfers + 1));
	if (!devc->transfers) {
		sr_err("USB transfers malloc failed.");
		return SR_ERR_MALLOC;
	}

	devc->logic_unitsize = logic_unitsize(channel_mask);
	devc->packed_buffer_size = DSLOGIC_ATOMIC_SAMPLES *
		(size / (channel_count * DSLOGIC_ATOMIC_BYTES) + 1) * devc->logic_unitsize;
	devc->packed_buffer = g_try_malloc(devc->packed_buffer_size);
	if (!devc->packed_buffer) {
		sr_err("Deinterleave buffer malloc failed.");
		release_unsubmitted_transfers(devc);
		return SR_ERR_MALLOC;
	}

	devc->num_transfers = num_transfers + 1;
	/* Allocate every transfer before submitting any of them. */
	{
		unsigned char *header = g_try_malloc0(dslogic_trigger_header_size(devc));
		if (!header) {
			release_unsubmitted_transfers(devc);
			return SR_ERR_MALLOC;
		}
		transfer = libusb_alloc_transfer(0);
		if (!transfer) {
			g_free(header);
			release_unsubmitted_transfers(devc);
			return SR_ERR_MALLOC;
		}
		libusb_fill_bulk_transfer(transfer, usb->devhdl,
			6 | LIBUSB_ENDPOINT_IN, header,
			dslogic_trigger_header_size(devc), trigger_receive,
			(void *)sdi, 0);
		devc->transfers[0] = transfer;
	}
	for (i = 0; i < num_transfers; i++) {
		if (!(buf = g_try_malloc(size))) {
			sr_err("USB transfer buffer malloc failed.");
			release_unsubmitted_transfers(devc);
			return SR_ERR_MALLOC;
		}
		transfer = libusb_alloc_transfer(0);
		if (!transfer) {
			g_free(buf);
			release_unsubmitted_transfers(devc);
			return SR_ERR_MALLOC;
		}
		libusb_fill_bulk_transfer(transfer, usb->devhdl,
				6 | LIBUSB_ENDPOINT_IN, buf, size,
				receive_transfer, (void *)sdi, timeout);
		devc->transfers[i + 1] = transfer;
	}
	for (i = 0; i < devc->num_transfers; i++) {
		transfer = devc->transfers[i];
		sr_info("submitting transfer: %d", i);
		if ((ret = libusb_submit_transfer(transfer)) != 0) {
			sr_err("Failed to submit transfer: %s.",
			       libusb_error_name(ret));
			discard_unsubmitted_transfer(transfer);
			devc->transfers[i] = NULL;
			while (++i < devc->num_transfers) {
				discard_unsubmitted_transfer(devc->transfers[i]);
				devc->transfers[i] = NULL;
			}
			cancel_and_drain_transfers((struct sr_dev_inst *)sdi);
			if (!devc->submitted_transfers)
				release_unsubmitted_transfers(devc);
			return SR_ERR;
		}
		devc->submitted_transfers++;
	}

	return SR_OK;
}

static void LIBUSB_CALL trigger_receive(struct libusb_transfer *transfer)
{
	const struct sr_dev_inst *sdi;
	struct dslogic_trigger_header *header;
	struct dev_context *devc;
	uint64_t remain_count;

	sdi = transfer->user_data;
	devc = sdi->priv;
	if (devc->acq_aborted) {
		free_transfer(transfer);
		return;
	}
	if (transfer->status == LIBUSB_TRANSFER_CANCELLED) {
		sr_dbg("Trigger transfer canceled.");
	} else if (transfer->status == LIBUSB_TRANSFER_COMPLETED &&
			transfer->actual_length == (int)dslogic_trigger_header_size(devc)) {
		header = (struct dslogic_trigger_header *)transfer->buffer;
		if (GUINT32_FROM_LE(header->check_id) != DSLOGIC_TRIGGER_CHECK_ID) {
			sr_err("Invalid DSLogic trigger header check id.");
			devc->acquisition_state = DSLOGIC_ACQ_ERROR;
			devc->acq_aborted = TRUE;
		} else {
			remain_count = GUINT32_FROM_LE(header->remain_cnt_h);
			remain_count = (remain_count << 32) |
				GUINT32_FROM_LE(header->remain_cnt_l);
			if (!dslogic_trigger_header_starts_data(devc, remain_count)) {
				sr_dbg("Ignoring DSLogic trigger header outside buffered capture: "
					"remaining samples %" PRIu64 ".", remain_count);
				devc->acquisition_state = DSLOGIC_ACQ_ERROR;
				free_transfer(transfer);
				return;
			}
			if (!dslogic_profile_operation_is_stream(devc->profile,
					devc->operation_mode))
				devc->actual_limit_samples =
					(devc->actual_limit_samples - remain_count) &
					~(DSLOGIC_ATOMIC_SAMPLES - 1);
			sr_info("DSLogic trigger header: remaining samples %" PRIu64 ".",
				remain_count);
			devc->trigger_pos = 0;
			devc->acquisition_state = DSLOGIC_ACQ_DATA;
			std_session_send_df_trigger(sdi);
			free_transfer(transfer);
			return;
		}
	}

	/* A malformed or failed header is never interpreted as logic data. */
	if (transfer->status != LIBUSB_TRANSFER_CANCELLED) {
		sr_err("Failed to receive a complete DSLogic trigger header.");
		devc->acquisition_state = DSLOGIC_ACQ_ERROR;
	}
	devc->acq_aborted = TRUE;
	if (transfer->status != LIBUSB_TRANSFER_CANCELLED) {
		unsigned int index;
		for (index = 1; index < devc->num_transfers; index++)
			if (devc->transfers[index])
				libusb_cancel_transfer(devc->transfers[index]);
	}
	send_datafeed_end_once((struct sr_dev_inst *)sdi);
	free_transfer(transfer);
}

static int default_start_stop(void *data, const struct sr_dev_inst *sdi)
{
	(void)data;
	return command_stop_acquisition(sdi);
}

static int default_start_upload_fpga(void *data, const struct sr_dev_inst *sdi)
{
	(void)data;
	return dslogic_prepare_fpga(sdi);
}

static int default_start_apply_threshold(void *data,
		const struct sr_dev_inst *sdi, double threshold)
{
	(void)data;
	return dslogic_set_voltage_threshold(sdi, threshold);
}

static int default_start_configure_fpga(void *data, const struct sr_dev_inst *sdi)
{
	(void)data;
	return fpga_configure(sdi);
}

static int default_start_transfers(void *data, const struct sr_dev_inst *sdi)
{
	(void)data;
	return start_transfers(sdi);
}

static int default_start_add_event_source(void *data,
		const struct sr_dev_inst *sdi, unsigned int timeout)
{
	struct dev_context *const devc = sdi->priv;
	(void)data;
	return usb_source_add(sdi->session, devc->ctx, timeout, receive_data,
		(void *)sdi);
}

static int default_start_start(void *data, const struct sr_dev_inst *sdi)
{
	(void)data;
	return command_start_acquisition(sdi);
}

static void default_start_send_header(void *data, const struct sr_dev_inst *sdi)
{
	(void)data;
	std_session_send_df_header(sdi);
}

static void default_start_remove_event_source(void *data,
		const struct sr_dev_inst *sdi)
{
	const struct dev_context *const devc = sdi->priv;
	(void)data;
	usb_source_remove(sdi->session, devc->ctx);
}

static void default_start_cancel_and_drain(void *data,
		const struct sr_dev_inst *sdi)
{
	(void)data;
	cancel_and_drain_transfers((struct sr_dev_inst *)sdi);
}

static int default_start_recover(void *data, const struct sr_dev_inst *sdi)
{
	(void)data;
	return dslogic_recover_device_state(sdi);
}

static const struct dslogic_acquisition_start_ops default_acquisition_start_ops = {
	.stop = default_start_stop,
	.upload_fpga = default_start_upload_fpga,
	.apply_threshold = default_start_apply_threshold,
	.configure_fpga = default_start_configure_fpga,
	.start_transfers = default_start_transfers,
	.add_event_source = default_start_add_event_source,
	.start = default_start_start,
	.send_header = default_start_send_header,
	.remove_event_source = default_start_remove_event_source,
	.cancel_and_drain = default_start_cancel_and_drain,
	.recover = default_start_recover,
};

static int acquisition_start_failure_cleanup(const struct sr_dev_inst *sdi,
		const struct dslogic_acquisition_start_ops *ops, void *ops_data,
		int operation_error)
{
	struct dev_context *const devc = sdi->priv;
	int recovery_error;

	if (devc->event_source_registered) {
		ops->remove_event_source(ops_data, sdi);
		devc->event_source_registered = FALSE;
	}
	ops->cancel_and_drain(ops_data, sdi);
	recovery_error = ops->recover(ops_data, sdi);
	if (recovery_error != SR_OK) {
		devc->fpga_state = DSLOGIC_FPGA_RECOVERY_REQUIRED;
		return recovery_error;
	}
	return operation_error;
}

SR_PRIV int dslogic_acquisition_start_with_ops(const struct sr_dev_inst *sdi,
		const struct dslogic_acquisition_start_ops *ops, void *ops_data)
{
	unsigned int timeout;
	const unsigned int channel_count = enabled_channel_count(sdi);

	struct dev_context *devc;
	int ret;

	if (!sdi || !sdi->driver || !ops || !ops->stop || !ops->upload_fpga ||
			!ops->apply_threshold || !ops->configure_fpga ||
			!ops->start_transfers || !ops->add_event_source || !ops->start ||
			!ops->send_header || !ops->remove_event_source ||
			!ops->cancel_and_drain || !ops->recover)
		return SR_ERR_ARG;
	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;
	/* Reject malformed local state before emitting the first USB command. */
	if (!devc->profile || !sdi->conn || !channel_count ||
			channel_count > devc->profile->logic_channel_count ||
			!devc->cur_samplerate ||
			!dslogic_profile_operation_supported(devc->profile,
				devc->operation_mode))
		return SR_ERR_ARG;
	devc->ctx = ((struct drv_context *)sdi->driver->context)->sr_ctx;
	devc->sent_samples = 0;
	devc->empty_transfer_count = 0;
	devc->empty_poll_count = 0;
	devc->transfer_completed = FALSE;
	devc->overflow_seen = FALSE;
	devc->acq_aborted = FALSE;
	devc->datafeed_end_sent = FALSE;
	devc->event_source_registered = FALSE;
	devc->cross_remainder_length = 0;
	devc->acquisition_state = DSLOGIC_ACQ_START;

	if ((ret = ops->stop(ops_data, sdi)) != SR_OK)
		return ret;
	devc->actual_limit_samples = devc->limit_samples ?
		(devc->limit_samples + DSLOGIC_ATOMIC_SAMPLES - 1) &
		~(DSLOGIC_ATOMIC_SAMPLES - 1) : 0;
	if (devc->operation_mode == DSLOGIC_OPERATION_INTERNAL_TEST)
		devc->selected_mode = dslogic_profile_mode_for_operation(devc->profile,
			devc->operation_mode);
	else
		devc->selected_mode = dslogic_profile_select_mode_for_operation(
			devc->profile, channel_count, devc->cur_samplerate,
			dslogic_profile_operation_is_stream(devc->profile,
				devc->operation_mode));
	if (!devc->selected_mode) {
		sr_err("No DSLogic channel mode supports %u channels at samplerate %" PRIu64
			" in %s mode.", channel_count, devc->cur_samplerate,
		dslogic_profile_operation_is_stream(devc->profile,
			devc->operation_mode) ? "stream" : "buffer");
		return SR_ERR_ARG;
	}
	if (devc->fpga_needs_upload &&
			(ret = ops->upload_fpga(ops_data, sdi)) != SR_OK)
		return ret;
	if (devc->threshold_needs_apply &&
			(ret = ops->apply_threshold(ops_data, sdi,
				devc->cur_threshold)) != SR_OK)
		return ret;
	devc->threshold_needs_apply = FALSE;
	timeout = get_timeout(sdi);

	if ((ret = ops->configure_fpga(ops_data, sdi)) != SR_OK)
		return ret;
	devc->acquisition_state = DSLOGIC_ACQ_READY;
	if ((ret = ops->start_transfers(ops_data, sdi)) != SR_OK)
		return acquisition_start_failure_cleanup(sdi, ops, ops_data, ret);

	if ((ret = ops->add_event_source(ops_data, sdi, timeout)) != SR_OK)
		return acquisition_start_failure_cleanup(sdi, ops, ops_data, ret);
	devc->event_source_registered = TRUE;

	if ((ret = ops->start(ops_data, sdi)) != SR_OK)
		return acquisition_start_failure_cleanup(sdi, ops, ops_data, ret);
	devc->acquisition_state = DSLOGIC_ACQ_TRIGGERED;

	/* The standard stream header is emitted after START, before trigger/data. */
	ops->send_header(ops_data, sdi);

	return ret;
}

SR_PRIV int dslogic_acquisition_start(const struct sr_dev_inst *sdi)
{
	return dslogic_acquisition_start_with_ops(sdi,
		&default_acquisition_start_ops, NULL);
}

static void default_stop_cancel_and_drain(void *data,
		const struct sr_dev_inst *sdi)
{
	(void)data;
	cancel_and_drain_transfers((struct sr_dev_inst *)sdi);
}

static const struct dslogic_acquisition_stop_ops default_acquisition_stop_ops = {
	.recover = default_start_recover,
	.cancel_and_drain = default_stop_cancel_and_drain,
};

SR_PRIV int dslogic_acquisition_stop_with_ops(const struct sr_dev_inst *sdi,
		const struct dslogic_acquisition_stop_ops *ops, void *ops_data)
{
	struct dev_context *devc;
	int ret;

	if (!sdi || !sdi->priv || !ops || !ops->recover ||
			!ops->cancel_and_drain)
		return SR_ERR_ARG;
	devc = sdi->priv;
	devc->acquisition_state = DSLOGIC_ACQ_STOP;
	/* DSView sets abort before FORCE_RDY so its reply cannot become a trigger. */
	devc->acq_aborted = TRUE;
	ret = ops->recover(ops_data, sdi);
	ops->cancel_and_drain(ops_data, sdi);
	return ret;
}

SR_PRIV int dslogic_acquisition_stop(struct sr_dev_inst *sdi)
{
	return dslogic_acquisition_stop_with_ops(sdi, &default_acquisition_stop_ops,
		NULL);
}
