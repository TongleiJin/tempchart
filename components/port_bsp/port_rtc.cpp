#include "port_rtc.h"

#include <cstring>

#include "epaper_config.h"
#include "pcf85063a.h"
#include "port_i2c.h"

static pcf85063a_dev_t s_rtc;
static bool s_rtc_ready;

static esp_err_t ensure_rtc_ready(void)
{
    if (s_rtc_ready) {
        return ESP_OK;
    }

    I2cMasterBus *bus = I2cMasterBus::requestInstance(
        ESP32_I2C_SCL_PIN, ESP32_I2C_SDA_PIN, ESP32_I2C_DEV_NUM);
    if (bus == NULL) {
        return ESP_FAIL;
    }

    esp_err_t ret = pcf85063a_init(&s_rtc, bus->Get_I2cBusHandle(), PCF85063A_ADDRESS);
    if (ret == ESP_OK) {
        s_rtc_ready = true;
    }
    return ret;
}

static pcf85063a_datetime_t tm_to_rtc(const struct tm *time)
{
    pcf85063a_datetime_t rtc_time = {};
    rtc_time.year = (uint16_t)(time->tm_year + 1900);
    rtc_time.month = (uint8_t)(time->tm_mon + 1);
    rtc_time.day = (uint8_t)time->tm_mday;
    rtc_time.dotw = (uint8_t)time->tm_wday;
    rtc_time.hour = (uint8_t)time->tm_hour;
    rtc_time.min = (uint8_t)time->tm_min;
    rtc_time.sec = (uint8_t)time->tm_sec;
    return rtc_time;
}

static void rtc_to_tm(const pcf85063a_datetime_t *rtc_time, struct tm *out_time)
{
    memset(out_time, 0, sizeof(*out_time));
    out_time->tm_year = rtc_time->year - 1900;
    out_time->tm_mon = rtc_time->month - 1;
    out_time->tm_mday = rtc_time->day;
    out_time->tm_wday = rtc_time->dotw;
    out_time->tm_hour = rtc_time->hour;
    out_time->tm_min = rtc_time->min;
    out_time->tm_sec = rtc_time->sec;
}

bool PortRtc_IsReady(void)
{
    return ensure_rtc_ready() == ESP_OK;
}

esp_err_t PortRtc_GetLocalTime(struct tm *out_time)
{
    if (out_time == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ensure_rtc_ready();
    if (ret != ESP_OK) {
        return ret;
    }

    pcf85063a_datetime_t rtc_time = {};
    ret = pcf85063a_get_time_date(&s_rtc, &rtc_time);
    if (ret != ESP_OK) {
        return ret;
    }

    rtc_to_tm(&rtc_time, out_time);
    return ESP_OK;
}

esp_err_t PortRtc_SetLocalTime(const struct tm *time)
{
    if (time == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ensure_rtc_ready();
    if (ret != ESP_OK) {
        return ret;
    }

    pcf85063a_datetime_t rtc_time = tm_to_rtc(time);
    return pcf85063a_set_time_date(&s_rtc, rtc_time);
}

// set date only to rtc, keep time unchanged
esp_err_t PortRtc_SetLocalDate(const struct tm *date)
{
    if (date == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ensure_rtc_ready();
    if (ret != ESP_OK) {
        return ret;
    }

    pcf85063a_datetime_t rtc_time = {};
    ret = pcf85063a_get_time_date(&s_rtc, &rtc_time);
    if (ret != ESP_OK) {
        return ret;
    }

    rtc_time.year = (uint16_t)(date->tm_year + 1900);
    rtc_time.month = (uint8_t)(date->tm_mon + 1);
    rtc_time.day = (uint8_t)date->tm_mday;
    rtc_time.dotw = (uint8_t)date->tm_wday;

    return pcf85063a_set_time_date(&s_rtc, rtc_time);
}

// set time only to rtc, keep date unchanged
esp_err_t PortRtc_SetLocalTimeOnly(const struct tm *time)
{
    if (time == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ensure_rtc_ready();
    if (ret != ESP_OK) {
        return ret;
    }

    pcf85063a_datetime_t rtc_time = {};
    ret = pcf85063a_get_time_date(&s_rtc, &rtc_time);
    if (ret != ESP_OK) {
        return ret;
    }

    rtc_time.hour = (uint8_t)time->tm_hour;
    rtc_time.min = (uint8_t)time->tm_min;
    rtc_time.sec = (uint8_t)time->tm_sec;

    return pcf85063a_set_time_date(&s_rtc, rtc_time);
}   
