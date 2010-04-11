/*
 * Eee PC WMI hotkey driver
 *
 * Copyright(C) 2010 Intel Corporation.
 *
 * Portions based on wistron_btns.c:
 * Copyright (C) 2005 Miloslav Trmac <mitr@volny.cz>
 * Copyright (C) 2005 Bernhard Rosenkraenzer <bero@arklinux.org>
 * Copyright (C) 2005 Dmitry Torokhov <dtor@mail.ru>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/input/sparse-keymap.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>

MODULE_AUTHOR("Yong Wang <yong.y.wang@intel.com>");
MODULE_DESCRIPTION("Eee PC WMI Hotkey Driver");
MODULE_LICENSE("GPL");

#define EEEPC_WMI_EVENT_GUID	"ABBC0F72-8EA1-11D1-00A0-C90629100000"

MODULE_ALIAS("wmi:"EEEPC_WMI_EVENT_GUID);

#define NOTIFY_BRNUP_MIN	0x11
#define NOTIFY_BRNUP_MAX	0x1f
#define NOTIFY_BRNDOWN_MIN	0x20
#define NOTIFY_BRNDOWN_MAX	0x2e

static const struct key_entry eeepc_wmi_keymap[] = {
	/* Sleep already handled via generic ACPI code */
	{ KE_KEY, 0x5d, { KEY_WLAN } },
	{ KE_KEY, 0x32, { KEY_MUTE } },
	{ KE_KEY, 0x31, { KEY_VOLUMEDOWN } },
	{ KE_KEY, 0x30, { KEY_VOLUMEUP } },
	{ KE_IGNORE, NOTIFY_BRNDOWN_MIN, { KEY_BRIGHTNESSDOWN } },
	{ KE_IGNORE, NOTIFY_BRNUP_MIN, { KEY_BRIGHTNESSUP } },
	{ KE_KEY, 0xcc, { KEY_SWITCHVIDEOMODE } },
	{ KE_END, 0},
};

struct eeepc_wmi {
	struct input_dev *inputdev;
};

static struct eeepc_wmi *eeepc;

static void eeepc_wmi_notify(u32 value, void *context)
{
	struct eeepc_wmi *eeepc = context;
	struct acpi_buffer response = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	acpi_status status;
	int code;

	status = wmi_get_event_data(value, &response);
	if (status != AE_OK) {
		pr_err("bad event status 0x%x\n", status);
		return;
	}

	obj = (union acpi_object *)response.pointer;

	if (obj && obj->type == ACPI_TYPE_INTEGER) {
		code = obj->integer.value;

		if (code >= NOTIFY_BRNUP_MIN && code <= NOTIFY_BRNUP_MAX)
			code = NOTIFY_BRNUP_MIN;
		else if (code >= NOTIFY_BRNDOWN_MIN && code <= NOTIFY_BRNDOWN_MAX)
			code = NOTIFY_BRNDOWN_MIN;

		if (!sparse_keymap_report_event(eeepc->inputdev,
						code, 1, true))
			pr_info("Unknown key %x pressed\n", code);
	}

	kfree(obj);
}

static int eeepc_wmi_input_init(struct eeepc_wmi *eeepc)
{
	int err;

	eeepc->inputdev = input_allocate_device();
	if (!eeepc->inputdev)
		return -ENOMEM;

	eeepc->inputdev->name = "Eee PC WMI hotkeys";
	eeepc->inputdev->phys = "wmi/input0";
	eeepc->inputdev->id.bustype = BUS_HOST;

	err = sparse_keymap_setup(eeepc->inputdev, eeepc_wmi_keymap, NULL);
	if (err)
		goto err_free_dev;

	err = input_register_device(eeepc->inputdev);
	if (err)
		goto err_free_keymap;

	return 0;

err_free_keymap:
	sparse_keymap_free(eeepc->inputdev);
err_free_dev:
	input_free_device(eeepc->inputdev);
	return err;
}

static void eeepc_wmi_input_exit(struct eeepc_wmi *eeepc)
{
	if (eeepc->inputdev) {
		sparse_keymap_free(eeepc->inputdev);
		input_unregister_device(eeepc->inputdev);
	}

	eeepc->inputdev = NULL;
}

static int __init eeepc_wmi_init(void)
{
	int err;
	acpi_status status;

	if (!wmi_has_guid(EEEPC_WMI_EVENT_GUID)) {
		pr_warning("No known WMI GUID found\n");
		return -ENODEV;
	}

	eeepc = kzalloc(sizeof(struct eeepc_wmi), GFP_KERNEL);
	if (!eeepc)
		return -ENOMEM;

	err = eeepc_wmi_input_init(eeepc);
	if (err) {
		kfree(eeepc);
		return err;
	}

	status = wmi_install_notify_handler(EEEPC_WMI_EVENT_GUID,
					eeepc_wmi_notify, eeepc);
	if (ACPI_FAILURE(status)) {
		pr_err("Unable to register notify handler - %d\n",
			status);
		eeepc_wmi_input_exit(eeepc);
		kfree(eeepc);
		return -ENODEV;
	}

	return 0;
}

static void __exit eeepc_wmi_exit(void)
{
	wmi_remove_notify_handler(EEEPC_WMI_EVENT_GUID);
	eeepc_wmi_input_exit(eeepc);
	kfree(eeepc);
}

module_init(eeepc_wmi_init);
module_exit(eeepc_wmi_exit);
