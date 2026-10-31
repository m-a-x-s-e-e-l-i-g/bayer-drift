/* SPDX-License-Identifier: GPL-2.0-or-later; Max Seelig (MAXmade). */
#ifndef BAYER_SETTINGS_H
#define BAYER_SETTINGS_H
#include "fx_bayer.h"
#define bayer_settings_load FX(settings_load)
#define bayer_settings_save FX(settings_save)
typedef struct {unsigned enabled,preset,strength,pattern,size,phase;} bayer_settings;
static inline fx_params bayer_settings_params(const bayer_settings*s)
{fx_params p={{s->strength,s->pattern,s->size,s->phase}};return p;}
static inline int bayer_settings_equal(const bayer_settings*a,const bayer_settings*b)
{return a->enabled==b->enabled&&a->preset==b->preset&&a->strength==b->strength&&a->pattern==b->pattern&&a->size==b->size&&a->phase==b->phase;}
static inline void bayer_settings_preset(bayer_settings*s,unsigned preset)
{
    static const unsigned strengths[]={1,4,8},sizes[]={0,0,1};
    if(preset<3){s->preset=preset;s->strength=strengths[preset];s->pattern=0;s->size=sizes[preset];s->phase=0;}
}
static inline int bayer_settings_valid(const bayer_settings*s)
{fx_params p=bayer_settings_params(s);return s->enabled<=1&&s->preset<=3&&fx_bayer_validate(&p);}
int bayer_settings_load(bayer_settings*);
int bayer_settings_save(const bayer_settings*);
#endif
