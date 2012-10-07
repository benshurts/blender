/*
 * ***** BEGIN GPL LICENSE BLOCK *****
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
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2012 Blender Foundation.
 * All rights reserved.
 *
 *
 * Contributor(s): Blender Foundation,
 *                 Sergey Sharybin
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/editors/mask/mask_draw.c
 *  \ingroup edmask
 */

#include "MEM_guardedalloc.h"

#include "BLI_utildefines.h"
#include "BLI_math.h"
#include "BLI_rect.h"

#include "BKE_context.h"
#include "BKE_mask.h"

#include "DNA_mask_types.h"
#include "DNA_screen_types.h"
#include "DNA_object_types.h"   /* SELECT */

#include "ED_mask.h"  /* own include */
#include "ED_space_api.h"

#include "GPU_colors.h"
#include "GPU_compatibility.h"

#include "UI_resources.h"
#include "UI_view2d.h"

#include "mask_intern.h"  /* own include */

static void mask_spline_color_get(MaskLayer *masklay, MaskSpline *spline, const int is_sel,
                                  unsigned char r_rgb[4])
{
	if (is_sel) {
		if (masklay->act_spline == spline) {
			r_rgb[0] = r_rgb[1] = r_rgb[2] = 255;
		}
		else {
			r_rgb[0] = 255;
			r_rgb[1] = r_rgb[2] = 0;
		}
	}
	else {
		r_rgb[0] = 128;
		r_rgb[1] = r_rgb[2] = 0;
	}

	r_rgb[3] = 255;
}

static void mask_spline_feather_color_get(MaskLayer *UNUSED(masklay), MaskSpline *UNUSED(spline), const int is_sel,
                                          unsigned char r_rgb[4])
{
	if (is_sel) {
		r_rgb[1] = 255;
		r_rgb[0] = r_rgb[2] = 0;
	}
	else {
		r_rgb[1] = 128;
		r_rgb[0] = r_rgb[2] = 0;
	}

	r_rgb[3] = 255;
}

#if 0
static void draw_spline_parents(MaskLayer *UNUSED(masklay), MaskSpline *spline)
{
	int i;
	MaskSplinePoint *points_array = BKE_mask_spline_point_array(spline);

	if (!spline->tot_point)
		return;

	gpuCurrentColor3x(CPACK_BLACK);
	glEnable(GL_LINE_STIPPLE);
	glLineStipple(1, 0xAAAA);

	gpuBegin(GL_LINES);

	for (i = 0; i < spline->tot_point; i++) {
		MaskSplinePoint *point = &points_array[i];
		BezTriple *bezt = &point->bezt;

		if (point->parent.id) {
			gpuVertex2f(bezt->vec[1][0],
			           bezt->vec[1][1]);

			gpuVertex2f(bezt->vec[1][0] - point->parent.offset[0],
			           bezt->vec[1][1] - point->parent.offset[1]);
		}
	}

	gpuEnd();

	glDisable(GL_LINE_STIPPLE);
}
#endif

/* return non-zero if spline is selected */
static void draw_spline_points(MaskLayer *masklay, MaskSpline *spline,
                               const char UNUSED(draw_flag), const char draw_type)
{
	const int is_spline_sel = (spline->flag & SELECT) && (masklay->restrictflag & MASK_RESTRICT_SELECT) == 0;
	unsigned char rgb_spline[4];
	MaskSplinePoint *points_array = BKE_mask_spline_point_array(spline);

	int i, hsize, tot_feather_point;
	float (*feather_points)[2], (*fp)[2];

	if (!spline->tot_point)
		return;

	/* TODO, add this to sequence editor */
	hsize = 4; /* UI_GetThemeValuef(TH_HANDLE_VERTEX_SIZE); */

	glPointSize(hsize);

	mask_spline_color_get(masklay, spline, is_spline_sel, rgb_spline);

	/* feather points */

	feather_points = fp = BKE_mask_spline_feather_points(spline, &tot_feather_point);

	gpuImmediateFormat_C4_V2();
	gpuBegin(GL_POINTS);

	for (i = 0; i < spline->tot_point; i++) {

		/* watch it! this is intentionally not the deform array, only check for sel */
		MaskSplinePoint *point = &spline->points[i];

		int j;

		for (j = 0; j < point->tot_uw + 1; j++) {
			int sel = FALSE;

			if (j == 0) {
				sel = MASKPOINT_ISSEL_ANY(point);
			}
			else {
				sel = point->uw[j - 1].flag & SELECT;
			}

			if (sel) {
				if (point == masklay->act_point)
					gpuColor3x(CPACK_WHITE);
				else
					gpuColor3x(CPACK_YELLOW);
			}
			else {
				gpuColor3f(0.5f, 0.5f, 0.0f);
			}

			gpuVertex2fv(*fp);

			fp++;
		}
	}

	gpuEnd();

	MEM_freeN(feather_points);

	/* control points */
	for (i = 0; i < spline->tot_point; i++) {

		/* watch it! this is intentionally not the deform array, only check for sel */
		MaskSplinePoint *point = &spline->points[i];
		MaskSplinePoint *point_deform = &points_array[i];
		BezTriple *bezt = &point_deform->bezt;

		float handle[2];
		float *vert = bezt->vec[1];
		int has_handle = BKE_mask_point_has_handle(point);

		BKE_mask_point_handle(point_deform, handle);

		/* draw handle segment */
		if (has_handle) {

			/* this could be split into its own loop */
			if (draw_type == MASK_DT_OUTLINE) {
				glLineWidth(3);
				gpuCurrentGray3f(0.376f);
				gpuBegin(GL_LINES);
				gpuVertex2fv(vert);
				gpuVertex2fv(handle);
				gpuEnd();
				glLineWidth(1);
			}

			gpuCurrentColor3ubv(rgb_spline);
			gpuBegin(GL_LINES);
			gpuVertex2fv(vert);
			gpuVertex2fv(handle);
			gpuEnd();
		}

		gpuBegin(GL_POINTS);

		/* draw CV point */
		if (MASKPOINT_ISSEL_KNOT(point)) {
			if (point == masklay->act_point)
				gpuColor3x(CPACK_WHITE);
			else
				gpuColor3x(CPACK_YELLOW);
		}
		else {
			gpuColor3f(0.5f, 0.5f, 0.0f);
		}

		gpuVertex2fv(vert);

		/* draw handle points */
		if (has_handle) {
			if (MASKPOINT_ISSEL_HANDLE(point)) {
				if (point == masklay->act_point)
					gpuColor3x(CPACK_WHITE);
				else
					gpuColor3x(CPACK_YELLOW);
			}
			else {
				gpuColor3f(0.5f, 0.5f, 0.0f);
			}

			gpuVertex2fv(handle);
		}

		gpuEnd();
	}

	glPointSize(1.0f);

	gpuImmediateUnformat();
}

/* #define USE_XOR */

static void mask_color_active_tint(unsigned char r_rgb[4], const unsigned char rgb[4], const short is_active)
{
	if (!is_active) {
		r_rgb[0] = (unsigned char)((((int)(rgb[0])) + 128) / 2);
		r_rgb[1] = (unsigned char)((((int)(rgb[1])) + 128) / 2);
		r_rgb[2] = (unsigned char)((((int)(rgb[2])) + 128) / 2);
		r_rgb[3] = rgb[3];
	}
	else {
		*(unsigned int *)r_rgb = *(const unsigned int *)rgb;
	}
}

static void mask_draw_curve_type(MaskSpline *spline, float (*points)[2], int tot_point,
                                 const short is_feather, const short is_smooth, const short is_active,
                                 const unsigned char rgb_spline[4], const char draw_type)
{
	const int draw_method = (spline->flag & MASK_SPLINE_CYCLIC) ? GL_LINE_LOOP : GL_LINE_STRIP;
	const unsigned char rgb_black[4] = {0x00, 0x00, 0x00, 0xff};
//	const unsigned char rgb_white[4] = {0xff, 0xff, 0xff, 0xff};
	unsigned char rgb_tmp[4];
	GPUarrays arrays = GPU_ARRAYS_V3F;

	arrays.vertexPointer = points;

	gpuImmediateFormat_V3();

	switch (draw_type) {

		case MASK_DT_OUTLINE:
			glLineWidth(3);

			mask_color_active_tint(rgb_tmp, rgb_black, is_active);
			gpuCurrentColor4ubv(rgb_tmp);
			gpuDrawClientArrays(draw_method, &arrays, 0, tot_point);

			glLineWidth(1);
			mask_color_active_tint(rgb_tmp, rgb_spline, is_active);
			gpuCurrentColor4ubv(rgb_tmp);
			gpuRepeat();
			break;

		case MASK_DT_DASH:
		default:
			glEnable(GL_LINE_STIPPLE);

#ifdef USE_XOR
			glEnable(GL_COLOR_LOGIC_OP);
			glLogicOp(GL_OR);
#endif
			mask_color_active_tint(rgb_tmp, rgb_spline, is_active);
			gpuCurrentColor4ubv(rgb_tmp);
			glLineStipple(3, 0xaaaa);
			gpuDrawClientArrays(draw_method, &arrays, 0, tot_point);

#ifdef USE_XOR
			glDisable(GL_COLOR_LOGIC_OP);
#endif
			mask_color_active_tint(rgb_tmp, rgb_black, is_active);
			gpuCurrentColor4ubv(rgb_tmp);
			glLineStipple(3, 0x5555);
			gpuRepeat();

			glDisable(GL_LINE_STIPPLE);
			break;


		case MASK_DT_BLACK:
		case MASK_DT_WHITE:
			if (draw_type == MASK_DT_BLACK) { rgb_tmp[0] = rgb_tmp[1] = rgb_tmp[2] = 0;   }
			else                            { rgb_tmp[0] = rgb_tmp[1] = rgb_tmp[2] = 255; }
			/* alpha values seem too low but gl draws many points that compensate for it */
			if (is_feather) { rgb_tmp[3] = 64; }
			else            { rgb_tmp[3] = 128; }

			if (is_feather) {
				rgb_tmp[0] = (unsigned char)(((short)rgb_tmp[0] + (short)rgb_spline[0]) / 2);
				rgb_tmp[1] = (unsigned char)(((short)rgb_tmp[1] + (short)rgb_spline[1]) / 2);
				rgb_tmp[2] = (unsigned char)(((short)rgb_tmp[2] + (short)rgb_spline[2]) / 2);
			}

			if (is_smooth == FALSE && is_feather) {
				glEnable(GL_BLEND);
			}

			mask_color_active_tint(rgb_tmp, rgb_tmp, is_active);
			gpuCurrentColor4ubv(rgb_tmp);

			gpuDrawClientArrays(draw_method, &arrays, 0, tot_point);
			gpuRepeat(); // XXX: why twice?

			if (is_smooth == FALSE && is_feather) {
				glDisable(GL_BLEND);
			}

			break;
	}

	gpuImmediateUnformat();
}

static void draw_spline_curve(MaskLayer *masklay, MaskSpline *spline,
                              const char draw_flag, const char draw_type,
                              const short is_active,
                              int width, int height)
{
	const unsigned int resol = maxi(BKE_mask_spline_feather_resolution(spline, width, height),
	                                BKE_mask_spline_resolution(spline, width, height));

	unsigned char rgb_tmp[4];

	const short is_spline_sel = (spline->flag & SELECT) && (masklay->restrictflag & MASK_RESTRICT_SELECT) == 0;
	const short is_smooth = (draw_flag & MASK_DRAWFLAG_SMOOTH);
	const short is_fill = (spline->flag & MASK_SPLINE_NOFILL) == 0;

	int tot_diff_point;
	float (*diff_points)[2];

	int tot_feather_point;
	float (*feather_points)[2];

	diff_points = BKE_mask_spline_differentiate_with_resolution_ex(spline, &tot_diff_point, resol);

	if (!diff_points)
		return;

	if (is_smooth) {
		glEnable(GL_LINE_SMOOTH);
		glEnable(GL_BLEND);
	}

	feather_points = BKE_mask_spline_feather_differentiated_points_with_resolution_ex(spline, &tot_feather_point, resol, (is_fill != FALSE));

	/* draw feather */
	mask_spline_feather_color_get(masklay, spline, is_spline_sel, rgb_tmp);
	mask_draw_curve_type(spline, feather_points, tot_feather_point,
	                     TRUE, is_smooth, is_active,
	                     rgb_tmp, draw_type);

	if (!is_fill) {

		float *fp         = &diff_points[0][0];
		float *fp_feather = &feather_points[0][0];
		float tvec[2];
		int i;

		BLI_assert(tot_diff_point == tot_feather_point);

		for (i = 0; i < tot_diff_point; i++, fp += 2, fp_feather += 2) {
			sub_v2_v2v2(tvec, fp, fp_feather);
			add_v2_v2v2(fp_feather, fp, tvec);
		}

		/* same as above */
		mask_draw_curve_type(spline, feather_points, tot_feather_point,
		                     TRUE, is_smooth, is_active,
		                     rgb_tmp, draw_type);
	}

	MEM_freeN(feather_points);

	/* draw main curve */
	mask_spline_color_get(masklay, spline, is_spline_sel, rgb_tmp);
	mask_draw_curve_type(spline, diff_points, tot_diff_point,
	                     FALSE, is_smooth, is_active,
	                     rgb_tmp, draw_type);
	MEM_freeN(diff_points);

	if (draw_flag & MASK_DRAWFLAG_SMOOTH) {
		glDisable(GL_LINE_SMOOTH);
		glDisable(GL_BLEND);
	}

	(void)draw_type;
}

static void draw_masklays(Mask *mask, const char draw_flag, const char draw_type,
                          int width, int height)
{
	MaskLayer *masklay;
	int i;

	for (masklay = mask->masklayers.first, i = 0; masklay; masklay = masklay->next, i++) {
		MaskSpline *spline;
		const short is_active = (i == mask->masklay_act);

		if (masklay->restrictflag & MASK_RESTRICT_VIEW) {
			continue;
		}

		for (spline = masklay->splines.first; spline; spline = spline->next) {

			/* draw curve itself first... */
			draw_spline_curve(masklay, spline, draw_flag, draw_type, is_active, width, height);

//			draw_spline_parents(masklay, spline);

			if (!(masklay->restrictflag & MASK_RESTRICT_SELECT)) {
				/* ...and then handles over the curve so they're nicely visible */
				draw_spline_points(masklay, spline, draw_flag, draw_type);
			}

			/* show undeform for testing */
			if (0) {
				void *back = spline->points_deform;

				spline->points_deform = NULL;
				draw_spline_curve(masklay, spline, draw_flag, draw_type, is_active, width, height);
//				draw_spline_parents(masklay, spline);
				draw_spline_points(masklay, spline, draw_flag, draw_type);
				spline->points_deform = back;
			}
		}
	}
}

void ED_mask_draw(const bContext *C,
                  const char draw_flag, const char draw_type)
{
	ScrArea *sa = CTX_wm_area(C);

	Mask *mask = CTX_data_edit_mask(C);
	int width, height;

	if (!mask)
		return;

	ED_mask_get_size(sa, &width, &height);

	draw_masklays(mask, draw_flag, draw_type, width, height);
}

/* sets up the opengl context.
 * width, height are to match the values from ED_mask_get_size() */
void ED_mask_draw_region(Mask *mask, ARegion *ar,
                         const char draw_flag, const char draw_type,
                         const int width_i, const int height_i,  /* convert directly into aspect corrected vars */
                         const float aspx, const float aspy,
                         const short do_scale_applied, const short do_post_draw,
                         float stabmat[4][4], /* optional - only used by clip */
                         const bContext *C    /* optional - only used when do_post_draw is set */
                         )
{
	struct View2D *v2d = &ar->v2d;

	/* aspect always scales vertically in movie and image spaces */
	const float width = width_i, height = (float)height_i * (aspy / aspx);

	int x, y;
	/* int w, h; */
	float zoomx, zoomy;

	/* frame image */
	float maxdim;
	float xofs, yofs;

	/* find window pixel coordinates of origin */
	UI_view2d_to_region_no_clip(&ar->v2d, 0.0f, 0.0f, &x, &y);


	/* w = BLI_rctf_size_x(&v2d->tot); */
	/* h = BLI_rctf_size_y(&v2d->tot);/*/


	zoomx = (float)(BLI_rcti_size_x(&ar->winrct) + 1) / BLI_rctf_size_x(&ar->v2d.cur);
	zoomy = (float)(BLI_rcti_size_y(&ar->winrct) + 1) / BLI_rctf_size_y(&ar->v2d.cur);

	if (do_scale_applied) {
		zoomx /= width;
		zoomy /= height;
	}

	x += v2d->tot.xmin * zoomx;
	y += v2d->tot.ymin * zoomy;

	/* frame the image */
	maxdim = maxf(width, height);
	if (width == height) {
		xofs = yofs = 0;
	}
	else if (width < height) {
		xofs = ((height - width) / -2.0f) * zoomx;
		yofs = 0.0f;
	}
	else { /* (width > height) */
		xofs = 0.0f;
		yofs = ((width - height) / -2.0f) * zoomy;
	}

	/* apply transformation so mask editing tools will assume drawing from the origin in normalized space */
	glPushMatrix();
	glTranslatef(x + xofs, y + yofs, 0);
	glScalef(maxdim * zoomx, maxdim * zoomy, 0);

	if (stabmat) {
		gpuMultMatrix(stabmat);
	}

	/* draw! */
	draw_masklays(mask, draw_flag, draw_type, width, height);

	if (do_post_draw) {
		ED_region_draw_cb_draw(C, ar, REGION_DRAW_POST_VIEW);
	}

	glPopMatrix();
}

void ED_mask_draw_frames(Mask *mask, ARegion *ar, const int cfra, const int sfra, const int efra)
{
	const float framelen = ar->winx / (float)(efra - sfra + 1);

	MaskLayer *masklay = BKE_mask_layer_active(mask);

	gpuCurrentColor4ub(255, 175, 0, 255);

	gpuImmediateFormat_V2();
	gpuBegin(GL_LINES);

	if (masklay) {
		MaskLayerShape *masklay_shape;

		for (masklay_shape = masklay->splines_shapes.first;
		     masklay_shape;
		     masklay_shape = masklay_shape->next)
		{
			int frame = masklay_shape->frame;

			/* draw_keyframe(i, CFRA, sfra, framelen, 1); */
			int height = (frame == cfra) ? 22 : 10;
			int x = (frame - sfra) * framelen;
			gpuVertex2i(x, 0);
			gpuVertex2i(x, height);
		}
	}

	gpuEnd();
	gpuImmediateUnlock();
}
