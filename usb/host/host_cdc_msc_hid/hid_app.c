/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021, Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "bsp/board.h"
#include "tusb.h"

//--------------------------------------------------------------------+
// MACRO TYPEDEF CONSTANT ENUM DECLARATION
//--------------------------------------------------------------------+

// If your host terminal support ansi escape code such as TeraTerm
// it can be use to simulate mouse cursor movement within terminal
#define USE_ANSI_ESCAPE   0

#define MAX_REPORT  4

static uint8_t const keycode2ascii[128][2] =  { HID_KEYCODE_TO_ASCII };

// Each HID instance can has multiple reports
static struct
{
  uint8_t report_count;
  tuh_hid_report_info_t report_info[MAX_REPORT];
}hid_info[CFG_TUH_HID];

static void process_kbd_report(hid_keyboard_report_t const *report);
static void process_mouse_report(hid_mouse_report_t const * report);
static void process_generic_report(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len);
static void process_sony_ds4(uint8_t const *report, uint16_t len);
static bool is_sony_ds4(uint8_t dev_addr);

void hid_app_task(void) {
  // nothing to do
}

//--------------------------------------------------------------------+
// TinyUSB Callbacks
//--------------------------------------------------------------------+

// Invoked when device with hid interface is mounted
// Report descriptor is also available for use. tuh_hid_parse_report_descriptor()
// can be used to parse common/simple enough descriptor.
// Note: if report descriptor length > CFG_TUH_ENUMERATION_BUFSIZE, it will be skipped
// therefore report_desc = NULL, desc_len = 0
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len) {
  (void)desc_report;
  (void)desc_len;
  uint16_t vid, pid;
  tuh_vid_pid_get(dev_addr, &vid, &pid);

  printf("HID device address = %d, instance = %d is mounted\r\n", dev_addr, instance);
  printf("VID = %04x, PID = %04x\r\n", vid, pid);

  // Interface protocol (hid_interface_protocol_enum_t)
  const char* protocol_str[] = { "None", "Keyboard", "Mouse" };
  uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

  printf("HID Interface Protocol = %s\r\n", protocol_str[itf_protocol]);

  // By default host stack will use activate boot protocol on supported interface.
  // Therefore for this simple example, we only need to parse generic report descriptor (with built-in parser)
  if (itf_protocol == HID_ITF_PROTOCOL_NONE) {
    hid_info[instance].report_count = tuh_hid_parse_report_descriptor(hid_info[instance].report_info, MAX_REPORT, desc_report, desc_len);
    printf("HID has %u reports \r\n", hid_info[instance].report_count);
  }

  // request to receive report
  // tuh_hid_report_received_cb() will be invoked when report is available
  if (!tuh_hid_receive_report(dev_addr, instance)) {
    printf("Error: cannot request to receive report\r\n");
  }
}

// Invoked when device with hid interface is un-mounted
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
  printf("HID device address = %d, instance = %d is unmounted\r\n", dev_addr, instance);
}

// Invoked when received report from device via interrupt endpoint
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len) {
  uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

  switch (itf_protocol) {
  case HID_ITF_PROTOCOL_KEYBOARD:
    TU_LOG2("HID receive boot keyboard report\r\n");
    process_kbd_report((hid_keyboard_report_t const*) report);
    break;

  case HID_ITF_PROTOCOL_MOUSE:
    TU_LOG2("HID receive boot mouse report\r\n");
    process_mouse_report((hid_mouse_report_t const*) report);
    break;

  default:
    if (is_sony_ds4(dev_addr)) {
      process_sony_ds4(report, len);
    } else {
      // Generic report requires matching ReportID and contents with previous parsed report info
      process_generic_report(dev_addr, instance, report, len);
    }
    break;
  }

  // continue to request to receive report
  if (!tuh_hid_receive_report(dev_addr, instance)) {
    printf("Error: cannot request to receive report\r\n");
  }
}

//--------------------------------------------------------------------+
// Keyboard
//--------------------------------------------------------------------+

// look up new key in previous keys
static inline bool find_key_in_report(hid_keyboard_report_t const *report, uint8_t keycode) {
  for (uint8_t i = 0; i < 6; i++) {
    if (report->keycode[i] == keycode) {
      return true;
    }
  }

  return false;
}

static void process_kbd_report(hid_keyboard_report_t const *report) {
  static hid_keyboard_report_t prev_report = { 0, 0, { 0 } }; // previous report to check key released

  //------------- example code ignore control (non-printable) key affects -------------//
  for (uint8_t i = 0; i < 6; i++) {
    if (report->keycode[i]) {
      if (find_key_in_report(&prev_report, report->keycode[i])) {
        // exist in previous report means the current key is holding
      } else {
        // not existed in previous report means the current key is pressed
        bool const is_shift = report->modifier
            & (KEYBOARD_MODIFIER_LEFTSHIFT | KEYBOARD_MODIFIER_RIGHTSHIFT);
        uint8_t ch = keycode2ascii[report->keycode[i]][is_shift ? 1 : 0];
        putchar(ch);
        if (ch == '\r') {
          putchar('\n');       // added new line for enter key
        }
        fflush(stdout); // flush right away, else nanolib will wait for newline
      }
    }
    // TODO example skips key released
  }
  prev_report = *report;
}

//--------------------------------------------------------------------+
// Mouse
//--------------------------------------------------------------------+

void cursor_movement(int8_t x, int8_t y, int8_t wheel) {
#if USE_ANSI_ESCAPE
  // Move X using ansi escape
  if (x < 0) {
    printf(ANSI_CURSOR_BACKWARD(%d), (-x)); // move left
  } else if (x > 0) {
    printf(ANSI_CURSOR_FORWARD(%d), x); // move right
  }

  // Move Y using ansi escape
  if (y < 0) {
    printf(ANSI_CURSOR_UP(%d), (-y)); // move up
    } else if (y > 0) {
    printf(ANSI_CURSOR_DOWN(%d), y); // move down
  }

  // Scroll using ansi escape
  if (wheel < 0) {
    printf(ANSI_SCROLL_UP(%d), (-wheel)); // scroll up
  } else if (wheel > 0) {
    printf(ANSI_SCROLL_DOWN(%d), wheel); // scroll down
  }

  printf("\r\n");
#else
  printf("(%d %d %d)\r\n", x, y, wheel);
#endif
}

static void process_mouse_report(hid_mouse_report_t const *report) {
  static hid_mouse_report_t prev_report = { 0 };

  //------------- button state  -------------//
  uint8_t button_changed_mask = report->buttons ^ prev_report.buttons;
  if (button_changed_mask & report->buttons) {
    printf(" %c%c%c ",
        report->buttons & MOUSE_BUTTON_LEFT ? 'L' : '-',
        report->buttons & MOUSE_BUTTON_MIDDLE ? 'M' : '-',
        report->buttons & MOUSE_BUTTON_RIGHT ? 'R' : '-');
  }

  //------------- cursor movement -------------//
  cursor_movement(report->x, report->y, report->wheel);
}

//--------------------------------------------------------------------+
// Generic Report
//--------------------------------------------------------------------+
static void process_generic_report(uint8_t dev_addr, uint8_t instance,
    uint8_t const *report, uint16_t len) {
  (void) dev_addr;

  uint8_t const rpt_count = hid_info[instance].report_count;
  tuh_hid_report_info_t *rpt_info_arr = hid_info[instance].report_info;
  tuh_hid_report_info_t *rpt_info = NULL;

  if (rpt_count == 1 && rpt_info_arr[0].report_id == 0) {
    // Simple report without report ID as 1st byte
    rpt_info = &rpt_info_arr[0];
  } else {
    // Composite report, 1st byte is report ID, data starts from 2nd byte
    uint8_t const rpt_id = report[0];

    // Find report id in the arrray
    for (uint8_t i = 0; i < rpt_count; i++) {
      if (rpt_id == rpt_info_arr[i].report_id) {
        rpt_info = &rpt_info_arr[i];
        break;
      }
    }

    report++;
    len--;
  }

  if (!rpt_info) {
    printf("Couldn't find the report info for this report !\r\n");
    return;
  }

  // For complete list of Usage Page & Usage checkout src/class/hid/hid.h. For examples:
  // - Keyboard                     : Desktop, Keyboard
  // - Mouse                        : Desktop, Mouse
  // - Gamepad                      : Desktop, Gamepad
  // - Consumer Control (Media Key) : Consumer, Consumer Control
  // - System Control (Power key)   : Desktop, System Control
  // - Generic (vendor)             : 0xFFxx, xx
  if (rpt_info->usage_page == HID_USAGE_PAGE_DESKTOP) {
    switch (rpt_info->usage) {
    case HID_USAGE_DESKTOP_KEYBOARD:
      TU_LOG1("HID receive keyboard report\r\n");
      // Assume keyboard follow boot report layout
      process_kbd_report((hid_keyboard_report_t const*) report);
      break;

    case HID_USAGE_DESKTOP_MOUSE:
      TU_LOG1("HID receive mouse report\r\n");
      // Assume mouse follow boot report layout
      process_mouse_report((hid_mouse_report_t const*) report);
      break;

    default:
      break;
    }
  }
}

// --------------------------------------------------------------------+
// Gamepad
// --------------------------------------------------------------------+

/* From https://www.kernel.org/doc/html/latest/input/gamepad.html
          ____________________________              __
         / [__ZL__]          [__ZR__] \               |
        / [__ TL __]        [__ TR __] \              | Front Triggers
     __/________________________________\__         __|
    /                                  _   \          |
   /      /\           __             (N)   \         |
  /       ||      __  |MO|  __     _       _ \        | Main Pad
 |    <===DP===> |SE|      |ST|   (W) -|- (E) |       |
  \       ||    ___          ___       _     /        |
  /\      \/   /   \        /   \     (S)   /\      __|
 /  \________ | LS  | ____ |  RS | ________/  \       |
|         /  \ \___/ /    \ \___/ /  \         |      | Control Sticks
|        /    \_____/      \_____/    \        |    __|
|       /                              \       |
 \_____/                                \_____/

     |________|______|    |______|___________|
       D-Pad    Left       Right   Action Pad
               Stick       Stick

                 |_____________|
                    Menu Pad

  Most gamepads have the following features:
  - Action-Pad 4 buttons in diamonds-shape (on the right side) NORTH, SOUTH, WEST and EAST.
  - D-Pad (Direction-pad) 4 buttons (on the left side) that point up, down, left and right.
  - Menu-Pad Different constellations, but most-times 2 buttons: SELECT - START.
  - Analog-Sticks provide freely moveable sticks to control directions, Analog-sticks may also
  provide a digital button if you press them.
  - Triggers are located on the upper-side of the pad in vertical direction. The upper buttons
  are normally named Left- and Right-Triggers, the lower buttons Z-Left and Z-Right.
  - Rumble Many devices provide force-feedback features. But are mostly just simple rumble motors.
 */

// Sony DS4 report layout detail https://www.psdevwiki.com/ps4/DS4-USB
typedef struct TU_ATTR_PACKED {
  uint8_t x, y, z, rz; // joystick

  struct {
    uint8_t dpad :4; // (hat format, 0x08 is released, 0=N, 1=NE, 2=E, 3=SE, 4=S, 5=SW, 6=W, 7=NW)
    uint8_t square :1; // west
    uint8_t cross :1; // south
    uint8_t circle :1; // east
    uint8_t triangle :1; // north
  };

  struct {
    uint8_t l1 :1;
    uint8_t r1 :1;
    uint8_t l2 :1;
    uint8_t r2 :1;
    uint8_t share :1;
    uint8_t option :1;
    uint8_t l3 :1;
    uint8_t r3 :1;
  };

  struct {
    uint8_t ps :1; // playstation button
    uint8_t tpad :1; // track pad click
    uint8_t counter :6; // +1 each report
  };

// comment out since not used by this example
// uint8_t l2_trigger; // 0 released, 0xff fully pressed
// uint8_t r2_trigger; // as above

//  uint16_t timestamp;
//  uint8_t  battery;
//
//  int16_t gyro[3];  // x, y, z;
//  int16_t accel[3]; // x, y, z

// there is still lots more info

} sony_ds4_report_t;

// check if device is Sony DualShock 4
static bool is_sony_ds4(uint8_t dev_addr) {
  uint16_t vid, pid;
  tuh_vid_pid_get(dev_addr, &vid, &pid);

  return (vid == 0x054c && (pid == 0x09cc || pid == 0x05c4)) // Sony DualShock4
  || (vid == 0x0f0d && pid == 0x005e)       // Hori FC4
      || (vid == 0x0f0d && pid == 0x00ee)   // Hori PS4 Mini (PS4-099U)
      || (vid == 0x1f4f && pid == 0x1002)   // ASW GG xrd controller
  ;
}

// check if different than 2
bool diff_than_2(uint8_t x, uint8_t y) {
  return (x - y > 2) || (y - x > 2);
}

// check if 2 reports are different enough
bool diff_report(sony_ds4_report_t const *rpt1, sony_ds4_report_t const *rpt2) {
  bool result;

  // x, y, z, rz must different than 2 to be counted
  result = diff_than_2(rpt1->x, rpt2->x) || diff_than_2(rpt1->y, rpt2->y)
      || diff_than_2(rpt1->z, rpt2->z) || diff_than_2(rpt1->rz, rpt2->rz);

  // check the reset with mem compare
  result |= memcmp(&rpt1->rz + 1, &rpt2->rz + 1, sizeof(sony_ds4_report_t) - 4);

  return result;
}

void process_sony_ds4(uint8_t const *report, uint16_t len) {
  const char *dpad_str[] =
      { "N", "NE", "E", "SE", "S", "SW", "W", "NW", "none" };

  // previous report used to compare for changes
  static sony_ds4_report_t prev_report = { 0 };

  uint8_t const report_id = report[0];
  report++;
  len--;

  // all buttons state is stored in ID 1
  if (report_id == 1) {
    sony_ds4_report_t ds4_report;
    memcpy(&ds4_report, report, sizeof(ds4_report));

    // counter is +1, assign to make it easier to compare 2 report
    prev_report.counter = ds4_report.counter;

    // only print if changes since it is polled ~ 5ms
    // Since count+1 after each report and  x, y, z, rz fluctuate within 1 or 2
    // We need more than memcmp to check if report is different enough
    if (diff_report(&prev_report, &ds4_report)) {
      printf("(x, y, z, rz) = (%u, %u, %u, %u)\r\n", ds4_report.x, ds4_report.y,
          ds4_report.z, ds4_report.rz);
      printf("DPad = %s ", dpad_str[ds4_report.dpad]);

      if (ds4_report.square) {
        printf("Square ");
      }
      if (ds4_report.cross) {
        printf("Cross ");
      }
      if (ds4_report.circle) {
        printf("Circle ");
      }
      if (ds4_report.triangle) {
        printf("Triangle ");
      }

      if (ds4_report.l1) {
        printf("L1 ");
      }
      if (ds4_report.r1) {
        printf("R1 ");
      }
      if (ds4_report.l2) {
        printf("L2 ");
      }
      if (ds4_report.r2) {
        printf("R2 ");
      }

      if (ds4_report.share) {
        printf("Share ");
      }
      if (ds4_report.option) {
        printf("Option ");
      }
      if (ds4_report.l3) {
        printf("L3 ");
      }
      if (ds4_report.r3) {
        printf("R3 ");
      }

      if (ds4_report.ps) {
        printf("PS ");
      }
      if (ds4_report.tpad) {
        printf("TPad ");
      }

      printf("\r\n");
    }

    prev_report = ds4_report;
  }
}
