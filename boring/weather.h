#ifndef WEATHER_H
#define WEATHER_H

#include <stdbool.h>

typedef enum {
    WEATHER_CLEAR,
    WEATHER_PARTLY,
    WEATHER_OVERCAST,
    WEATHER_FOG,
    WEATHER_DRIZZLE,
    WEATHER_RAIN,
    WEATHER_SNOW,
    WEATHER_SHOWERS,
    WEATHER_THUNDERSTORM
} WeatherType;

typedef struct {
    double      temperature;    /* Celsius */
    double      windspeed;      /* km/h */
    WeatherType type;
    double      intensity;      /* 0.0 - 1.0 */
    int         cloudcover;     /* 0 - 100 percent */
    double      precipitation;  /* mm */
    bool        is_day;
    double      sunrise;        /* hours (e.g. 6.5 = 06:30) */
    double      sunset;         /* hours */
    bool        valid;
} WeatherData;

/*
 * Fetch current weather from Open-Meteo.
 * On failure, returns data with valid=false.
 */
WeatherData weather_fetch(double latitude, double longitude);

/* Human-readable description of the weather type */
const char *weather_description(WeatherType type);

/* Format sunrise/sunset hours as HH:MM string into buf (>= 6 bytes) */
void weather_format_time(double hours, char *buf, int bufsize);

#endif /* WEATHER_H */
