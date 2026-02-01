#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include <libsoup/soup.h>

#include "weather.h"
#include <cjson/cJSON.h>
#include "sunriset.h"

static WeatherType wmo_to_type(int code, double *intensity)
{
    *intensity = 0.0;

    switch (code) {
    case 0:
        return WEATHER_CLEAR;
    case 1:
        *intensity = 0.3;
        return WEATHER_PARTLY;
    case 2:
        *intensity = 0.6;
        return WEATHER_PARTLY;
    case 3:
        return WEATHER_OVERCAST;
    case 45:
    case 48:
        return WEATHER_FOG;
    case 51:
        *intensity = 0.3;
        return WEATHER_DRIZZLE;
    case 53:
        *intensity = 0.6;
        return WEATHER_DRIZZLE;
    case 55:
        *intensity = 1.0;
        return WEATHER_DRIZZLE;
    case 61:
        *intensity = 0.3;
        return WEATHER_RAIN;
    case 63:
        *intensity = 0.6;
        return WEATHER_RAIN;
    case 65:
        *intensity = 1.0;
        return WEATHER_RAIN;
    case 66:
        *intensity = 0.3;
        return WEATHER_RAIN;
    case 67:
        *intensity = 0.7;
        return WEATHER_RAIN;
    case 71:
        *intensity = 0.3;
        return WEATHER_SNOW;
    case 73:
        *intensity = 0.6;
        return WEATHER_SNOW;
    case 75:
        *intensity = 1.0;
        return WEATHER_SNOW;
    case 77:
        *intensity = 0.5;
        return WEATHER_SNOW;
    case 80:
        *intensity = 0.3;
        return WEATHER_SHOWERS;
    case 81:
        *intensity = 0.6;
        return WEATHER_SHOWERS;
    case 82:
        *intensity = 1.0;
        return WEATHER_SHOWERS;
    case 85:
        *intensity = 0.5;
        return WEATHER_SNOW;
    case 86:
        *intensity = 1.0;
        return WEATHER_SNOW;
    case 95:
        *intensity = 0.7;
        return WEATHER_THUNDERSTORM;
    case 96:
        *intensity = 0.8;
        return WEATHER_THUNDERSTORM;
    case 99:
        *intensity = 1.0;
        return WEATHER_THUNDERSTORM;
    default:
        return WEATHER_CLEAR;
    }
}

const char *weather_description(WeatherType type)
{
    switch (type) {
    case WEATHER_CLEAR:        return "Clear";
    case WEATHER_PARTLY:       return "Partly Cloudy";
    case WEATHER_OVERCAST:     return "Overcast";
    case WEATHER_FOG:          return "Fog";
    case WEATHER_DRIZZLE:      return "Drizzle";
    case WEATHER_RAIN:         return "Rain";
    case WEATHER_SNOW:         return "Snow";
    case WEATHER_SHOWERS:      return "Showers";
    case WEATHER_THUNDERSTORM: return "Thunderstorm";
    }
    return "Unknown";
}

void weather_format_time(double hours, char *buf, int bufsize)
{
    int h = (int)hours;
    int m = (int)((hours - h) * 60.0 + 0.5);

    if (m >= 60) {
        h++;
        m -= 60;
    }
    snprintf(buf, bufsize, "%02d:%02d", h % 24, m);
}

WeatherData weather_fetch(double latitude, double longitude)
{
    WeatherData data = { .valid = false };
    SoupSession *session;
    SoupMessage *msg;
    char url[512];

    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?"
             "latitude=%.4f&longitude=%.4f"
             "&current_weather=true"
             "&hourly=cloudcover,precipitation",
             latitude, longitude);

    session = soup_session_new();
    msg = soup_message_new("GET", url);
    if (!msg) {
        g_object_unref(session);
        return data;
    }

    GError *error = NULL;
    GInputStream *stream = soup_session_send(session, msg, NULL, &error);
    if (!stream || soup_message_get_status(msg) != 200) {
        g_clear_error(&error);
        g_clear_object(&stream);
        g_object_unref(msg);
        g_object_unref(session);
        return data;
    }

    /* Read entire response body */
    GByteArray *buf = g_byte_array_new();
    guint8 chunk[4096];
    gssize n;

    while ((n = g_input_stream_read(stream, chunk, sizeof(chunk), NULL, NULL)) > 0)
        g_byte_array_append(buf, chunk, n);
    g_object_unref(stream);

    g_byte_array_append(buf, (const guint8 *)"\0", 1);
    const char *body = (const char *)buf->data;

    cJSON *root = cJSON_Parse(body);
    g_byte_array_free(buf, TRUE);
    if (!root) {
        g_object_unref(msg);
        g_object_unref(session);
        return data;
    }

    /* Parse current_weather */
    cJSON *current = cJSON_GetObjectItem(root, "current_weather");
    if (current) {
        cJSON *temp = cJSON_GetObjectItem(current, "temperature");
        cJSON *wind = cJSON_GetObjectItem(current, "windspeed");
        cJSON *code = cJSON_GetObjectItem(current, "weathercode");
        cJSON *isday = cJSON_GetObjectItem(current, "is_day");

        if (temp) data.temperature = temp->valuedouble;
        if (wind) data.windspeed = wind->valuedouble;
        if (code) data.type = wmo_to_type(code->valueint, &data.intensity);
        if (isday) data.is_day = (isday->valueint != 0);

        data.valid = true;
    }

    /* Extract current hour's cloudcover and precipitation from hourly arrays */
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    int current_hour = tm_now->tm_hour;

    cJSON *hourly = cJSON_GetObjectItem(root, "hourly");
    if (hourly) {
        cJSON *cc_arr = cJSON_GetObjectItem(hourly, "cloudcover");
        cJSON *pr_arr = cJSON_GetObjectItem(hourly, "precipitation");

        if (cc_arr && cJSON_GetArraySize(cc_arr) > current_hour) {
            cJSON *cc = cJSON_GetArrayItem(cc_arr, current_hour);
            if (cc) data.cloudcover = cc->valueint;
        }
        if (pr_arr && cJSON_GetArraySize(pr_arr) > current_hour) {
            cJSON *pr = cJSON_GetArrayItem(pr_arr, current_hour);
            if (pr) data.precipitation = pr->valuedouble;
        }
    }

    cJSON_Delete(root);
    g_object_unref(msg);
    g_object_unref(session);

    /* Compute sunrise/sunset using vendored sunriset */
    {
        time_t t = time(NULL);
        struct tm *gm = gmtime(&t);
        int year = gm->tm_year + 1900;
        int month = gm->tm_mon + 1;
        int day = gm->tm_mday;

        double rise, set;
        sun_rise_set(year, month, day, longitude, latitude, &rise, &set);

        /* sun_rise_set returns UTC hours; convert to local */
        struct tm local_ref = *localtime(&t);
        struct tm utc_ref = *gmtime(&t);
        double tz_offset = difftime(mktime(&local_ref), mktime(&utc_ref)) / 3600.0;

        data.sunrise = fmod(rise + tz_offset + 24.0, 24.0);
        data.sunset = fmod(set + tz_offset + 24.0, 24.0);
    }

    return data;
}
