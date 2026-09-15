#ifndef FORECAST_H
#define FORECAST_H

#include <cairo.h>
#include "weather.h"

/*
 * Draw the hours ahead over whatever is already on the context, which
 * is the same animated sky the current conditions sit on.
 */
void forecast_draw(const WeatherData *weather, cairo_t *cr, int width, int height);

#endif /* FORECAST_H */
