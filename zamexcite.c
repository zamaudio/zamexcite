/* zamexcite.c  ZamCompX2 stereo compressor
 * Copyright (C) 2013  Damien Zammit
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <math.h>
#include <stdlib.h>

#include "lv2/lv2plug.in/ns/lv2core/lv2.h"

#define ZAMEXCITE_URI "http://zamaudio.com/lv2/zamexcite"

#define STEREOLINK_UNCOUPLED 0
#define STEREOLINK_AVERAGE 1
#define STEREOLINK_MAX 2
#define MAXDELAYSAMPLES 38400 /* 1/5 of a second at 192k */

#define min(x,y) (x < y) ? x : y

typedef enum {
	ZAMEXCITE_INPUT_L = 0,
	ZAMEXCITE_INPUT_R = 1,
	ZAMEXCITE_OUTPUT_L = 2,
	ZAMEXCITE_OUTPUT_R = 3,

	ZAMEXCITE_ATTACK = 4,
	ZAMEXCITE_RELEASE = 5,
	ZAMEXCITE_KNEE = 6,
	ZAMEXCITE_RATIO = 7,
	ZAMEXCITE_THRESHOLD = 8,
	ZAMEXCITE_MAKEUP = 9,
	
	ZAMEXCITE_GAINR_L = 10,
	ZAMEXCITE_GAINR_R = 11,

	ZAMEXCITE_STEREOLINK = 12,

	ZAMEXCITE_FINEDELAY = 13,
	ZAMEXCITE_HPFREQ = 14,
	ZAMEXCITE_DRYGAIN = 15,

	ZAMEXCITE_LISTEN = 16
} PortIndex;


typedef struct {
	float* input_l;
	float* input_r;
	float* output_l;
	float* output_r;
  
	float* attack;
	float* release;
	float* knee;
	float* ratio;
	float* threshold;
	float* makeup;
	
	float* drygain;
	float* finedelay;
	float* hpfreq;

	float* gainr_l;
	float* gainr_r;

	float* stereolink;
	float* listen;

	float srate;
	float oldL_yl;
	float oldR_yl;
	float oldL_y1;
	float oldR_y1;

	float xfl_1;
	float xfl_2;
	float yfr_1;
	float yfr_2;
	float xfr_1;
	float xfr_2;
	float yfl_1;
	float yfl_2;

	float delaybuf_l[MAXDELAYSAMPLES];
	float delaybuf_r[MAXDELAYSAMPLES];
	
	int delaychanged;
	int delaysamples;

} ZamEXCITE;

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor,
            double rate,
            const char* bundle_path,
            const LV2_Feature* const* features)
{
	ZamEXCITE* zamexcite = (ZamEXCITE*)malloc(sizeof(ZamEXCITE));
	zamexcite->srate = rate;
  
	zamexcite->oldL_yl=zamexcite->oldL_y1=0.f;
	zamexcite->oldR_yl=zamexcite->oldR_y1=0.f;

 	zamexcite->xfl_2 = zamexcite->xfl_1 = 0.f;
 	zamexcite->yfl_2 = zamexcite->yfl_1 = 0.f;
	 
	for (int i = 0; i < MAXDELAYSAMPLES; ++i) {
		zamexcite->delaybuf_l[i] = 0.f;
		zamexcite->delaybuf_r[i] = 0.f;
	}
	zamexcite->delaychanged = 0;
	zamexcite->delaysamples = 1;

	return (LV2_Handle)zamexcite;
}

static void
connect_port(LV2_Handle instance,
             uint32_t port,
             void* data)
{
	ZamEXCITE* zamexcite = (ZamEXCITE*)instance;
  
	switch ((PortIndex)port) {
	case ZAMEXCITE_INPUT_L:
		zamexcite->input_l = (float*)data;
  	break;
	case ZAMEXCITE_INPUT_R:
		zamexcite->input_r = (float*)data;
  	break;
	case ZAMEXCITE_OUTPUT_L:
		zamexcite->output_l = (float*)data;
  	break;
	case ZAMEXCITE_OUTPUT_R:
		zamexcite->output_r = (float*)data;
  	break;
	case ZAMEXCITE_ATTACK:
		zamexcite->attack = (float*)data;
	break;
	case ZAMEXCITE_RELEASE:
		zamexcite->release = (float*)data;
	break;
	case ZAMEXCITE_KNEE:
		zamexcite->knee = (float*)data;
	break;
	case ZAMEXCITE_RATIO:
		zamexcite->ratio = (float*)data;
	break;
	case ZAMEXCITE_THRESHOLD:
		zamexcite->threshold = (float*)data;
	break;
	case ZAMEXCITE_MAKEUP:
		zamexcite->makeup = (float*)data;
	break;
	case ZAMEXCITE_GAINR_L:
		zamexcite->gainr_l = (float*)data;
	break;
	case ZAMEXCITE_GAINR_R:
		zamexcite->gainr_r = (float*)data;
	break;
	case ZAMEXCITE_STEREOLINK:
		zamexcite->stereolink = (float*)data;
	break;
	case ZAMEXCITE_FINEDELAY:
		zamexcite->finedelay = (float*)data;
	break;
	case ZAMEXCITE_HPFREQ:
		zamexcite->hpfreq = (float*)data;
	break;
	case ZAMEXCITE_DRYGAIN:
		zamexcite->drygain = (float*)data;
	break;
	case ZAMEXCITE_LISTEN:
		zamexcite->listen = (float*)data;
	break;
	}
}

// Works on little-endian machines only
static inline bool
is_nan(float& value ) {
	if (((*(uint32_t *) &value) & 0x7fffffff) > 0x7f800000) {
		return true;
	}
return false;
}

// Force already-denormal float value to zero
static inline void
sanitize_denormal(float& value) {
	if (is_nan(value)) {
		value = 0.f;
	}
}

static inline float
from_dB(float gdb) {
	return (exp(gdb/20.f*log(10.f)));
};

static inline float
to_dB(float g) {
	return (20.f*log10(g));
}

static void
activate(LV2_Handle instance)
{
}

static void
run(LV2_Handle instance, uint32_t n_samples)
{
	ZamEXCITE* zamexcite = (ZamEXCITE*)instance;
  
	const float* const input_l = zamexcite->input_l;
	const float* const input_r = zamexcite->input_r;
	float* const output_l = zamexcite->output_l;
	float* const output_r = zamexcite->output_r;
  
	float attack = *(zamexcite->attack);
	float release = *(zamexcite->release);
	float knee = *(zamexcite->knee);
	float ratio = *(zamexcite->ratio);
	float threshold = from_dB(*(zamexcite->threshold));
	float makeup = from_dB(*(zamexcite->makeup));
	float* const gainr_l = zamexcite->gainr_l;
	float* const gainr_r = zamexcite->gainr_r;
	int stereolink = (*(zamexcite->stereolink) > 1.f) ? STEREOLINK_MAX : (*(zamexcite->stereolink) > 0.f) ? STEREOLINK_AVERAGE : STEREOLINK_UNCOUPLED;
	float width=(knee-0.99f)*6.f;
	float cdb=0.f;
	float attack_coeff = exp(-1000.f/(attack * zamexcite->srate));
	float release_coeff = exp(-1000.f/(release * zamexcite->srate));
	float thresdb = to_dB(threshold);

	float drygain = from_dB(*(zamexcite->drygain));
	int delaysamples = min(MAXDELAYSAMPLES, (int) (*(zamexcite->finedelay) * zamexcite->srate / 1000000));
	
	float togglelisten = (*(zamexcite->listen) > 0.1) ? 0.f : 1.f;

	zamexcite->delaysamples = delaysamples;
	if (zamexcite->delaychanged != delaysamples) {	
		for (int i = 0; i < delaysamples; ++i) {
			zamexcite->delaybuf_l[i] = 0.f;
			zamexcite->delaybuf_r[i] = 0.f;
		}
		zamexcite->delaychanged = delaysamples;
	}

	float Q = 0.5;
	float w0 = 2.0 * M_PI * *(zamexcite->hpfreq) / zamexcite->srate;
	float alpha = sin(w0)/(2.0*Q); 
	float b0 =  (1.0 + cos(w0))/2.0;
	float b1 = -(1.0 + cos(w0));
	float b2 =  (1.0 + cos(w0))/2.0;
	float a0 =   1.0 + alpha;
	float a1 =  -2.0 * cos(w0);
	float a2 =   1.0 - alpha;

	float Lgain = 1.f;
	float Rgain = 1.f;
	float Lxg, Lyg;
	float Rxg, Ryg;
	float Lxl, Lyl, Ly1;
	float Rxl, Ryl, Ry1;
 
	float tmpl, tmpr, intl, intr;

	for (uint32_t i = 0; i < n_samples; ++i) {
		float tmpinl=zamexcite->delaybuf_l[delaysamples-1];
		float tmpinr=zamexcite->delaybuf_r[delaysamples-1];

		sanitize_denormal(tmpinl);
		sanitize_denormal(tmpinr);

		sanitize_denormal(zamexcite->xfl_1);
		sanitize_denormal(zamexcite->xfl_2);
		sanitize_denormal(zamexcite->xfr_1);
		sanitize_denormal(zamexcite->xfr_2);
		sanitize_denormal(zamexcite->yfl_1);
		sanitize_denormal(zamexcite->yfl_2);
		sanitize_denormal(zamexcite->yfr_1);
		sanitize_denormal(zamexcite->yfr_2);
		
		intl = b0/a0*tmpinl + b1/a0*zamexcite->xfl_1 + b2/a0*zamexcite->xfl_2
			- a1/a0*zamexcite->yfl_1 - a2/a0*zamexcite->yfl_2;

		intr = b0/a0*tmpinr + b1/a0*zamexcite->xfr_1 + b2/a0*zamexcite->xfr_2
			- a1/a0*zamexcite->yfr_1 - a2/a0*zamexcite->yfr_2;

		sanitize_denormal(intl);
		sanitize_denormal(intr);

		Lyg = Ryg = 0.f;
		Lxg = (intl==0.f) ? -160.f : to_dB(fabs(intl));
		Rxg = (intr==0.f) ? -160.f : to_dB(fabs(intr));
		sanitize_denormal(Lxg);
		sanitize_denormal(Rxg);
    
    
		if (2.f*(Lxg-thresdb)<-width) {
			Lyg = Lxg;
		} else if (2.f*fabs(Lxg-thresdb)<=width) {
			Lyg = Lxg + (1.f/ratio-1.f)*(Lxg-thresdb+width/2.f)*(Lxg-thresdb+width/2.f)/(2.f*width);
		} else if (2.f*(Lxg-thresdb)>width) {
			Lyg = thresdb + (Lxg-thresdb)/ratio;
		}
    
		sanitize_denormal(Lyg);
    
		if (2.f*(Rxg-thresdb)<-width) {
			Ryg = Rxg;
		} else if (2.f*fabs(Rxg-thresdb)<=width) {
			Ryg = Rxg + (1.f/ratio-1.f)*(Rxg-thresdb+width/2.f)*(Rxg-thresdb+width/2.f)/(2.f*width);
		} else if (2.f*(Rxg-thresdb)>width) {
			Ryg = thresdb + (Rxg-thresdb)/ratio;
		}
    
		sanitize_denormal(Ryg);

		if (stereolink == STEREOLINK_UNCOUPLED) {
			Lxl = Lxg - Lyg;
			Rxl = Rxg - Ryg;
		} else if (stereolink == STEREOLINK_MAX) {
			Lxl = Rxl = fmaxf(Lxg - Lyg, Rxg - Ryg);
		} else {
			Lxl = Rxl = (Lxg - Lyg + Rxg - Ryg) / 2.f;
		}

		sanitize_denormal(zamexcite->oldL_y1);
		sanitize_denormal(zamexcite->oldR_y1);
		sanitize_denormal(zamexcite->oldL_yl);
		sanitize_denormal(zamexcite->oldR_yl);


		Ly1 = fmaxf(Lxl, release_coeff * zamexcite->oldL_y1+(1.f-release_coeff)*Lxl);
		Lyl = attack_coeff * zamexcite->oldL_yl+(1.f-attack_coeff)*Ly1;
		sanitize_denormal(Ly1);
		sanitize_denormal(Lyl);
    
		cdb = -Lyl;
		Lgain = from_dB(cdb);

		*gainr_l = Lyl;


		Ry1 = fmaxf(Rxl, release_coeff * zamexcite->oldR_y1+(1.f-release_coeff)*Rxl);
		Ryl = attack_coeff * zamexcite->oldR_yl+(1.f-attack_coeff)*Ry1;
		sanitize_denormal(Ry1);
		sanitize_denormal(Ryl);
    
		cdb = -Ryl;
		Rgain = from_dB(cdb);

		*gainr_r = Ryl;

		tmpl = (intl * Lgain * makeup);
		tmpr = (intr * Rgain * makeup);

		sanitize_denormal(tmpl);
		sanitize_denormal(tmpr);

		output_l[i] = tmpl + (input_l[i] * drygain) * togglelisten;
		output_r[i] = tmpr + (input_r[i] * drygain) * togglelisten;
    		
		//post
		zamexcite->oldL_yl = Lyl;
		zamexcite->oldR_yl = Ryl;
		zamexcite->oldL_y1 = Ly1;
		zamexcite->oldR_y1 = Ry1;
		
		zamexcite->xfl_2 = zamexcite->xfl_1;
		zamexcite->xfl_1 = tmpinl;

		zamexcite->yfl_2 = zamexcite->yfl_1;
		zamexcite->yfl_2 = tmpinl;

		zamexcite->xfr_2 = zamexcite->xfr_1;
		zamexcite->xfr_1 = tmpinr;

		zamexcite->yfr_2 = zamexcite->yfr_1;
		zamexcite->yfr_2 = tmpinr;

		zamexcite->delaybuf_l[0] = input_l[i];
		zamexcite->delaybuf_r[0] = input_r[i];

		for (int k = 0; k < delaysamples-1; ++k) {
			zamexcite->delaybuf_l[k+1] = zamexcite->delaybuf_l[k];
			zamexcite->delaybuf_r[k+1] = zamexcite->delaybuf_r[k];
		}
	}
}

static void
deactivate(LV2_Handle instance)
{
}

static void
cleanup(LV2_Handle instance)
{
	free(instance);
}

const void*
extension_data(const char* uri)
{
	return NULL;
}

static const LV2_Descriptor descriptor = {
	ZAMEXCITE_URI,
	instantiate,
	connect_port,
	activate,
	run,
	deactivate,
	cleanup,
	extension_data
};

LV2_SYMBOL_EXPORT
const LV2_Descriptor*
lv2_descriptor(uint32_t index)
{
	switch (index) {
	case 0:
		return &descriptor;
	default:
		return NULL;
	}
}
