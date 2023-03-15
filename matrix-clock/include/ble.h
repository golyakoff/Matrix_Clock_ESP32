#ifndef __BLE_H__
#define __BLE_H__

#include "time.h"

//BLE server name
#define BLE_SERVER_NAME                     "Tetris Clock A1"

#define DT_SERVICE_UUID                     "00001847-0000-1000-8000-00805F9B34FB"  // 0x1847

#define DEVICE_TIME_FEATURE_CHAR_UUID       "00002B8E-0000-1000-8000-00805F9B34FB"  // 0x2B8E M Read
#define DEVICE_TIME_PARAMETERS_CHAR_UUID    "00002B8F-0000-1000-8000-00805F9B34FB"  // 0x2B8F M Read
#define DEVICE_TIME_CHAR_UUID               "00002B90-0000-1000-8000-00805F9B34FB"  // 0x2B90 M Read, Indicate
#define DEVICE_TIME_CONTROL_POINT_CHAR_UUID "00002B91-0000-1000-8000-00805F9B34FB"  // 0x2B91 M Write, Indicate

// 0x2B92 Mandatory if the Time Change Logging feature is supported, otherwise Excluded Notify
// #define TIME_CHANGE_LOG_DATA_CHAR_UUID      "00002B92-0000-1000-8000-00805F9B34FB"

typedef enum
{
    // 0 : E2E-CRC. E2E_CRC field implemented in each characteristic within the service. O.
    DTF_E2E_CRC                     = 1 <<  0,
    // 1 : Time Change Logging. Time change logging is implemented to capture and preserve details of time change events O.
    DTF_TIME_CHANGE_LOGGING         = 1 <<  1,
    // 2 : Base Time Second-Fractions. Time values include Base Time fractions of a second
    //     (16-bit fractions of a second; 1/65,536 Second). O.
    DTF_BASE_SECOND_FRACTIONS       = 1 <<  2,
    // 3 : Time or Date Displayed to User. The device displays either time or date values or both. O.
    DTF_TIME_OR_DATE_DISPLAY        = 1 <<  3,
    // 4 : Displayed Formats. Formatting of the device’s displayed date and time is revealed using the DT Parameters characteristic. C1.
    DTF_DISPLAYED_FORMATS           = 1 <<  4,
    // 5 : Displayed Formats Changeable. The device can change the format of the displayed date or time and the Server 
    //     can indicate these format changes. C2.
    DTF_DISPLAYED_FORMAT_CHANGEABLE = 1 <<  5,
    // 6 : Separate User Timeline. The device supports allowing a user to set the time or date of the device (“User-facing Time”),
    //     creating a separate timeline from the synchronized Base Time. O.
    DTF_SEPARATE_USER_TIMELINE      = 1 <<  6,
    // 7 : Authorization Required. Authorization required to complete certain DTCP procedures. O.
    DTF_AUTHORIZATION_REQUIRED      = 1 <<  7,
    // 8 : RTC Drift Tracking. The Server supports monitoring RTC drift after being synchronized to a time source. O.
    DTF_RTC_DRIFT_TRACKING          = 1 <<  8,
    // 9 : Epoch Year 1900. The Server supports reporting and receiving Time Update procedures where the Base Time is based on Epoch 1900. C3.
    DTF_EPOCH_YEAR_1900             = 1 <<  9,
    // A : Epoch Year 2000. The Server supports reporting and receiving Time Update procedures where the Base Time is based on Epoch 2000. C3.
    DTF_EPOCH_YEAR_2000             = 1 << 10,
    // B : Propose Non-Logged Time Adjustment Limit. The Server supports changes to the DT Parameters field Non_Logged_Time_Adjustment_Limit using the DTCP. O.
    DTF_NON_LOGGED_TIME_ADJ_LIMIT   = 1 << 11,
    // C : Retrieve Active Time Adjustments. The Server supports the retrieval of either non-logged 
    //     Base_Time adjustments or consolidated Base_Time adjustments or both by using the DTCP to resolve a
    //     Base_Time value discrepancy. O.
    DTF_RETRIVE_ACT_TIME_ADJ        = 1 << 12
    // D-F: Reserved for Future Use.
} dtf_t;

struct dtp_t {
    uint16_t rtc_resolution;
    uint16_t dt_format;
};

void ble_init();
void ble_update_time(struct tm *dt);

#endif // __BLE_H__

