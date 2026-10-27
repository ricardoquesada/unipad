// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Ricardo Quesada
//
// BTstack dual-mode (Classic BR/EDR + BLE) configuration for Unipad C64 (Pico 2 W / RP2350).

#ifndef UNIPAD_C64_BTSTACK_CONFIG_H
#define UNIPAD_C64_BTSTACK_CONFIG_H

// Logging & diagnostics
#define ENABLE_LOG_INFO
#define ENABLE_LOG_ERROR
#define ENABLE_PRINTF_HEXDUMP
#define ENABLE_SCO_OVER_HCI

// Bluetooth Low Energy (BLE HID gamepads & mice: Xbox Series, modern 8BitDo, BLE mice, etc.)
#ifdef ENABLE_BLE
#define ENABLE_GATT_CLIENT_PAIRING
#define ENABLE_L2CAP_LE_CREDIT_BASED_FLOW_CONTROL_MODE
#define ENABLE_LE_CENTRAL
#define ENABLE_LE_DATA_LENGTH_EXTENSION
#define ENABLE_LE_PERIPHERAL
#define ENABLE_LE_PRIVACY_ADDRESS_RESOLUTION
#define ENABLE_LE_SECURE_CONNECTIONS
#else
#error "Unipad C64 requires ENABLE_BLE (link pico_btstack_ble)"
#endif

// Bluetooth Classic BR/EDR (DualShock 3/4, DualSense, Switch Pro, Wii/Wii U, 8BitDo Classic, etc.)
#ifdef ENABLE_CLASSIC
#define ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE
#define ENABLE_GOEP_L2CAP
#else
#error "Unipad C64 requires ENABLE_CLASSIC (link pico_btstack_classic)"
#endif

#if defined(ENABLE_CLASSIC) && defined(ENABLE_BLE)
#define ENABLE_CROSS_TRANSPORT_KEY_DERIVATION
#endif

// BTstack buffer and table sizing
#define HCI_OUTGOING_PRE_BUFFER_SIZE 4
#define HCI_ACL_PAYLOAD_SIZE (1691 + 4)
#define HCI_ACL_CHUNK_SIZE_ALIGNMENT 4
#define MAX_NR_AVDTP_CONNECTIONS 1
#define MAX_NR_AVDTP_STREAM_ENDPOINTS 1
#define MAX_NR_AVRCP_CONNECTIONS 2
#define MAX_NR_BNEP_CHANNELS 1
#define MAX_NR_BNEP_SERVICES 1
#define MAX_NR_BTSTACK_LINK_KEY_DB_MEMORY_ENTRIES 4
#define MAX_NR_GATT_CLIENTS 4
#define MAX_NR_HCI_CONNECTIONS 4
#define MAX_NR_HID_HOST_CONNECTIONS 4
#define MAX_NR_HIDS_HOSTS 4
#define MAX_NR_HFP_CONNECTIONS 1
#define MAX_NR_L2CAP_CHANNELS 8
#define MAX_NR_L2CAP_SERVICES 5
#define MAX_NR_RFCOMM_CHANNELS 1
#define MAX_NR_RFCOMM_MULTIPLEXERS 1
#define MAX_NR_RFCOMM_SERVICES 1
#define MAX_NR_SERVICE_RECORD_ITEMS 4
#define MAX_NR_SM_LOOKUP_ENTRIES 4
#define MAX_NR_WHITELIST_ENTRIES 16
#define MAX_NR_LE_DEVICE_DB_ENTRIES 16

// Flow-control tuning for CYW43439 shared gSPI bus
#define MAX_NR_CONTROLLER_ACL_BUFFERS 3
#define MAX_NR_CONTROLLER_SCO_PACKETS 3

#define ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL
#define HCI_HOST_ACL_PACKET_LEN 1024
#define HCI_HOST_ACL_PACKET_NUM 3
#define HCI_HOST_SCO_PACKET_LEN 120
#define HCI_HOST_SCO_PACKET_NUM 3

// Persistent Link Key DB, LE Device DB, and Bluepad32 properties stored via Flash Sector TLV
#define NVM_NUM_DEVICE_DB_ENTRIES 16
#define NVM_NUM_LINK_KEYS 16

// Static ATT database (no dynamic heap dependency)
#define MAX_ATT_DB_SIZE 512

// BTstack HAL bindings onto Pico SDK
#define HAVE_EMBEDDED_TIME_MS
#define HAVE_ASSERT
#define HCI_RESET_RESEND_TIMEOUT_MS 1000

#define ENABLE_SOFTWARE_AES128
#define ENABLE_MICRO_ECC_FOR_LE_SECURE_CONNECTIONS

#endif  // UNIPAD_C64_BTSTACK_CONFIG_H
