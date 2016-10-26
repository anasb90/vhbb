#include <global_include.h>

extern unsigned char _binary_assets_spr_img_itm_panel_png_start;
vita2d_texture *img_itm_panel;

int initListItem()
{
	img_itm_panel = vita2d_load_PNG_buffer(&_binary_assets_spr_img_itm_panel_png_start);

	return 0;
}

int displayListItem(int posY)
{
	vita2d_draw_texture(img_itm_panel, ITEM_POSX, posY);
	return 0;
}