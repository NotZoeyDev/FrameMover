#include "ui.h"
#include "pixels.h"
#include "i18n.h"

#include <cmath>

namespace
{
// Noto Sans CJK JP carries the Latin glyphs too; cairo's toy API does not fall back per glyph.
const char *FontFamily() { return i18n::Japanese() ? "Noto Sans CJK JP" : "Noto Sans"; }
} // namespace

Canvas::Canvas( int width, int height ) : m_w( width ), m_h( height )
{
	m_surface = cairo_image_surface_create( CAIRO_FORMAT_ARGB32, width, height );
	m_cr = cairo_create( m_surface );
}

Canvas::~Canvas()
{
	cairo_destroy( m_cr );
	cairo_surface_destroy( m_surface );
}

void Canvas::Clear( double r, double g, double b )
{
	cairo_set_source_rgb( m_cr, r, g, b );
	cairo_paint( m_cr );
}

void Canvas::RoundRect( double x, double y, double w, double h, double radius, double r, double g, double b )
{
	cairo_new_sub_path( m_cr );
	cairo_arc( m_cr, x + w - radius, y + radius, radius, -M_PI / 2, 0 );
	cairo_arc( m_cr, x + w - radius, y + h - radius, radius, 0, M_PI / 2 );
	cairo_arc( m_cr, x + radius, y + h - radius, radius, M_PI / 2, M_PI );
	cairo_arc( m_cr, x + radius, y + radius, radius, M_PI, 3 * M_PI / 2 );
	cairo_close_path( m_cr );
	cairo_set_source_rgb( m_cr, r, g, b );
	cairo_fill( m_cr );
}

void Canvas::Text( const std::string &s, double x, double y, double size, double r, double g, double b, bool bold )
{
	cairo_select_font_face( m_cr, FontFamily(), CAIRO_FONT_SLANT_NORMAL, bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL );
	cairo_set_font_size( m_cr, size );
	cairo_set_source_rgb( m_cr, r, g, b );
	cairo_move_to( m_cr, x, y );
	cairo_show_text( m_cr, s.c_str() );
}

double Canvas::TextWidth( const std::string &s, double size, bool bold )
{
	cairo_select_font_face( m_cr, FontFamily(), CAIRO_FONT_SLANT_NORMAL, bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL );
	cairo_set_font_size( m_cr, size );
	cairo_text_extents_t e;
	cairo_text_extents( m_cr, s.c_str(), &e );
	return e.x_advance;
}

void Canvas::CopyRGBA( uint8_t *dest ) const
{
	cairo_surface_flush( m_surface );
	const uint8_t *src = cairo_image_surface_get_data( m_surface );
	const int stride = cairo_image_surface_get_stride( m_surface );
	CopyARGB32ToRGBA( src, dest, m_w, m_h, stride );
}

bool Canvas::WritePng( const char *path ) const
{
	cairo_surface_flush( m_surface );
	return cairo_surface_write_to_png( m_surface, path ) == CAIRO_STATUS_SUCCESS;
}

int ButtonPanel::HitTest( double x, double y ) const
{
	for ( size_t i = 0; i < m_buttons.size(); i++ )
	{
		const Button &b = m_buttons[i];
		if ( x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h )
			return (int)i;
	}
	return -1;
}

void ButtonPanel::Draw( Canvas &c, int hovered ) const
{
	for ( size_t i = 0; i < m_buttons.size(); i++ )
	{
		const Button &b = m_buttons[i];
		const bool hot = (int)i == hovered;
		double r = 0.16, g = 0.19, bl = 0.25;
		if ( b.active )
			r = 0.15, g = 0.42, bl = 0.72;
		if ( hot )
			r += 0.1, g += 0.1, bl += 0.1;
		c.RoundRect( b.x, b.y, b.w, b.h, 10, r, g, bl );
		const double size = b.h * 0.42;
		c.Text( b.label, b.x + ( b.w - c.TextWidth( b.label, size ) ) / 2, b.y + b.h / 2 + size * 0.35, size, 1, 1, 1 );
	}
}
