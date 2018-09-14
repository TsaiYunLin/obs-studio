#include <graphics/math-defs.h>
#include <util/platform.h>
#include <util/util.hpp>
#include <obs-module.h>
#include <sys/stat.h>
#include <windows.h>
#include <gdiplus.h>
#include <algorithm>
#include <string>
#include <memory>

using namespace std;
using namespace Gdiplus;

#define warning(format, ...) blog(LOG_WARNING, "[%s] " format, \
		obs_source_get_name(source), ##__VA_ARGS__)

#define warn_stat(call) \
	do { \
		if (stat != Ok) \
			warning("%s: %s failed (%d)", __FUNCTION__, call, \
					(int)stat); \
	} while (false)

#ifndef clamp
#define clamp(val, min_val, max_val) \
	if (val < min_val) val = min_val; \
	else if (val > max_val) val = max_val;
#endif

#define MIN_SIZE_CX 2
#define MIN_SIZE_CY 2
#define MAX_SIZE_CX 16384
#define MAX_SIZE_CY 16384

#define MAX_AREA (4096LL * 4096LL)

#define VNR_SHM_SIZE 1024
#define VNR_SHM TEXT("Local\\VNR_PlotText")
#define VNR_SHM_MUTEX TEXT("Local\\VNR_SHM_MUTEX")
#define VNR_kyob1010_MultipleStream 0

/* ------------------------------------------------------------------------- */

#define S_FONT                          "font"
#define S_USE_FILE                      "read_from_file"
#define S_USE_SONG                      "get_playing_song"
#define S_USE_VNR                       "read_from_vnr"
#if VNR_kyob1010_MultipleStream
#define S_VNR_MODE                      "vnr_mode"
#endif
#define S_FILE                          "file"
#define S_TEXT                          "text"
#define S_COLOR                         "color"
#define S_GRADIENT                      "gradient"
#define S_GRADIENT_COLOR                "gradient_color"
#define S_GRADIENT_DIR                  "gradient_dir"
#define S_GRADIENT_OPACITY              "gradient_opacity"
#define S_ALIGN                         "align"
#define S_VALIGN                        "valign"
#define S_OPACITY                       "opacity"
#define S_BKCOLOR                       "bk_color"
#define S_BKOPACITY                     "bk_opacity"
#define S_VERTICAL                      "vertical"
#define S_OUTLINE                       "outline"
#define S_OUTLINE_SIZE                  "outline_size"
#define S_OUTLINE_COLOR                 "outline_color"
#define S_OUTLINE_OPACITY               "outline_opacity"
#define S_CHATLOG_MODE                  "chatlog"
#define S_CHATLOG_LINES                 "chatlog_lines"
#define S_EXTENTS                       "extents"
#define S_EXTENTS_WRAP                  "extents_wrap"
#define S_EXTENTS_CX                    "extents_cx"
#define S_EXTENTS_CY                    "extents_cy"

#define S_ALIGN_LEFT                    "left"
#define S_ALIGN_CENTER                  "center"
#define S_ALIGN_RIGHT                   "right"

#define S_VALIGN_TOP                    "top"
#define S_VALIGN_CENTER                 S_ALIGN_CENTER
#define S_VALIGN_BOTTOM                 "bottom"

#define T_(v)                           obs_module_text(v)
#define T_FONT                          T_("Font")
#define T_USE_FILE                      T_("ReadFromFile")
#define T_FILE                          T_("TextFile")
#define T_USE_SONG                      "GetPlayingSong"
#define T_USE_VNR                       "ReadFromVNR"
#define T_VNR_MODE                      "VNR text mode"
#define T_TEXT                          T_("Text")
#define T_COLOR                         T_("Color")
#define T_GRADIENT                      T_("Gradient")
#define T_GRADIENT_COLOR                T_("Gradient.Color")
#define T_GRADIENT_DIR                  T_("Gradient.Direction")
#define T_GRADIENT_OPACITY              T_("Gradient.Opacity")
#define T_ALIGN                         T_("Alignment")
#define T_VALIGN                        T_("VerticalAlignment")
#define T_OPACITY                       T_("Opacity")
#define T_BKCOLOR                       T_("BkColor")
#define T_BKOPACITY                     T_("BkOpacity")
#define T_VERTICAL                      T_("Vertical")
#define T_OUTLINE                       T_("Outline")
#define T_OUTLINE_SIZE                  T_("Outline.Size")
#define T_OUTLINE_COLOR                 T_("Outline.Color")
#define T_OUTLINE_OPACITY               T_("Outline.Opacity")
#define T_CHATLOG_MODE                  T_("ChatlogMode")
#define T_CHATLOG_LINES                 T_("ChatlogMode.Lines")
#define T_EXTENTS                       T_("UseCustomExtents")
#define T_EXTENTS_WRAP                  T_("UseCustomExtents.Wrap")
#define T_EXTENTS_CX                    T_("Width")
#define T_EXTENTS_CY                    T_("Height")

#define T_FILTER_TEXT_FILES             T_("Filter.TextFiles")
#define T_FILTER_ALL_FILES              T_("Filter.AllFiles")

#define T_ALIGN_LEFT                    T_("Alignment.Left")
#define T_ALIGN_CENTER                  T_("Alignment.Center")
#define T_ALIGN_RIGHT                   T_("Alignment.Right")

#define T_VALIGN_TOP                    T_("VerticalAlignment.Top")
#define T_VALIGN_CENTER                 T_ALIGN_CENTER
#define T_VALIGN_BOTTOM                 T_("VerticalAlignment.Bottom")

/* ------------------------------------------------------------------------- */

static inline DWORD get_alpha_val(uint32_t opacity)
{
	return ((opacity * 255 / 100) & 0xFF) << 24;
}

static inline DWORD calc_color(uint32_t color, uint32_t opacity)
{
	return color & 0xFFFFFF | get_alpha_val(opacity);
}

static inline wstring to_wide(const char *utf8)
{
	wstring text;

	size_t len = os_utf8_to_wcs(utf8, 0, nullptr, 0);
	text.resize(len);
	if (len)
		os_utf8_to_wcs(utf8, 0, &text[0], len + 1);

	return text;
}

static inline uint32_t rgb_to_bgr(uint32_t rgb)
{
	return ((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb & 0xFF0000) >> 16);
}


/* ------------------------------------------------------------------------- */

template<typename T, typename T2, BOOL WINAPI deleter(T2)> class GDIObj {
	T obj = nullptr;

	inline GDIObj &Replace(T obj_)
	{
		if (obj) deleter(obj);
		obj = obj_;
		return *this;
	}

public:
	inline GDIObj() {}
	inline GDIObj(T obj_) : obj(obj_) {}
	inline ~GDIObj() {deleter(obj);}

	inline T operator=(T obj_) {Replace(obj_); return obj;}

	inline operator T() const {return obj;}

	inline bool operator==(T obj_) const {return obj == obj_;}
	inline bool operator!=(T obj_) const {return obj != obj_;}
};

using HDCObj = GDIObj<HDC, HDC, DeleteDC>;
using HFONTObj = GDIObj<HFONT, HGDIOBJ, DeleteObject>;
using HBITMAPObj = GDIObj<HBITMAP, HGDIOBJ, DeleteObject>;

/* ------------------------------------------------------------------------- */

enum class Align {
	Left,
	Center,
	Right
};

enum class VAlign {
	Top,
	Center,
	Bottom
};

struct TextSource {
	typedef bool (TextSource::*song_pfn)(const wchar_t * const);

	obs_source_t *source = nullptr;

	gs_texture_t *tex = nullptr;
	uint32_t cx = 0;
	uint32_t cy = 0;

	HDCObj hdc;
	Graphics graphics;

	HFONTObj hfont;
	unique_ptr<Font> font;

	string file;
	time_t file_timestamp = 0;
	bool update_file = false;
	float update_time_elapsed = 0.0f;

	HWND song_hwnd = NULL;
	song_pfn song_pfunc = nullptr;
	enum class Mode : unsigned char
		{ text, file, song, vnr } mode;

	bool last_use_vnr = false;
	const char *vnr_mode = nullptr;
	unsigned char vnr_id = 0;
	static unsigned char vnr_count;
	static HANDLE hMapFile;
	static HANDLE hMutex;
	static struct SHM {
		unsigned char *id;
		wchar_t *data;
	} shm;

	wstring text;
	wstring face;
	int face_size = 0;
	uint32_t color = 0xFFFFFF;
	uint32_t color2 = 0xFFFFFF;
	float gradient_dir = 0;
	uint32_t opacity = 100;
	uint32_t opacity2 = 100;
	uint32_t bk_color = 0;
	uint32_t bk_opacity = 0;
	Align align = Align::Left;
	VAlign valign = VAlign::Top;
	bool gradient = false;
	bool bold = false;
	bool italic = false;
	bool underline = false;
	bool strikeout = false;
	bool vertical = false;

	bool use_outline = false;
	float outline_size = 0.0f;
	uint32_t outline_color = 0;
	uint32_t outline_opacity = 100;

	bool use_extents = false;
	bool wrap = false;
	uint32_t extents_cx = 0;
	uint32_t extents_cy = 0;

	bool chatlog_mode = false;
	int chatlog_lines = 6;

	/* --------------------------- */

	inline TextSource(obs_source_t *source_, obs_data_t *settings) :
		source                (source_),
		hdc                   (CreateCompatibleDC(nullptr)),
		graphics              (hdc)
	{
		obs_source_update(source, settings);
	}

	inline ~TextSource()
	{
		if (tex) {
			obs_enter_graphics();
			gs_texture_destroy(tex);
			obs_leave_graphics();
		}
		if (mode == Mode::vnr) {
			--TextSource::vnr_count;
			TextSource::CloseSHM();
		}
	}

	void UpdateFont();
	void GetStringFormat(StringFormat &format);
	void RemoveNewlinePadding(const StringFormat &format, RectF &box);
	void CalculateTextSizes(const StringFormat &format,
			RectF &bounding_box, SIZE &text_size);
	void RenderOutlineText(Graphics &graphics,
			const GraphicsPath &path,
			const Brush &brush);
	void RenderText();
	void LoadFileText();

	const char *GetMainString(const char *str);

	inline void Update(obs_data_t *settings);
	inline void Tick(float seconds);
	inline void Render(gs_effect_t *effect);

	static BOOL CALLBACK find_target(HWND hwnd, LPARAM lParam);
	BOOL get_song_name(HWND hwnd);
	// song players
	static constexpr wchar_t *browsers[] =
		{ L"- Mozilla Firefox", L"- Google Chrome" };
	bool get_song_browser_player(const wchar_t * const title, song_pfn const pfn);
	bool get_song_browser_youtube(const wchar_t * const title);
	bool get_song_foobar2000(const wchar_t * const title);
	bool get_song_osu(const wchar_t * const title);
	void set_song_name(const wchar_t * const name);

	void VNR_initial(obs_data_t *s);
	void VNR_FallBackToText(obs_data_t *s);
	static void CloseSHM();
	void ReadFromVNR();
};
unsigned char TextSource::vnr_count = 0;
HANDLE TextSource::hMapFile = NULL;
HANDLE TextSource::hMutex = NULL;
struct TextSource::SHM TextSource::shm = { 0 };


static time_t get_modified_timestamp(const char *filename)
{
	struct stat stats;
	if (os_stat(filename, &stats) != 0)
		return -1;
	return stats.st_mtime;
}

void TextSource::UpdateFont()
{
	hfont = nullptr;
	font.reset(nullptr);

	LOGFONT lf = {};
	lf.lfHeight = face_size;
	lf.lfWeight = bold ? FW_BOLD : FW_DONTCARE;
	lf.lfItalic = italic;
	lf.lfUnderline = underline;
	lf.lfStrikeOut = strikeout;
	lf.lfQuality = ANTIALIASED_QUALITY;
	lf.lfCharSet = DEFAULT_CHARSET;

	if (!face.empty()) {
		wcscpy(lf.lfFaceName, face.c_str());
		hfont = CreateFontIndirect(&lf);
	}

	if (!hfont) {
		wcscpy(lf.lfFaceName, L"Arial");
		hfont = CreateFontIndirect(&lf);
	}

	if (hfont)
		font.reset(new Font(hdc, hfont));
}

void TextSource::GetStringFormat(StringFormat &format)
{
	UINT flags = StringFormatFlagsNoFitBlackBox |
		StringFormatFlagsMeasureTrailingSpaces;

	if (vertical)
		flags |= StringFormatFlagsDirectionVertical |
			StringFormatFlagsDirectionRightToLeft;

	format.SetFormatFlags(flags);
	format.SetTrimming(StringTrimmingWord);

	switch (align) {
	case Align::Left:
		if (vertical)
			format.SetLineAlignment(StringAlignmentFar);
		else
			format.SetAlignment(StringAlignmentNear);
		break;
	case Align::Center:
		if (vertical)
			format.SetLineAlignment(StringAlignmentCenter);
		else
			format.SetAlignment(StringAlignmentCenter);
		break;
	case Align::Right:
		if (vertical)
			format.SetLineAlignment(StringAlignmentNear);
		else
			format.SetAlignment(StringAlignmentFar);
	}

	switch (valign) {
	case VAlign::Top:
		if (vertical)
			format.SetAlignment(StringAlignmentNear);
		else
			format.SetLineAlignment(StringAlignmentNear);
		break;
	case VAlign::Center:
		if (vertical)
			format.SetAlignment(StringAlignmentCenter);
		else
			format.SetLineAlignment(StringAlignmentCenter);
		break;
	case VAlign::Bottom:
		if (vertical)
			format.SetAlignment(StringAlignmentFar);
		else
			format.SetLineAlignment(StringAlignmentFar);
	}
}

/* GDI+ treats '\n' as an extra character with an actual render size when
 * calculating the texture size, so we have to calculate the size of '\n' to
 * remove the padding.  Because we always add a newline to the string, we
 * also remove the extra unused newline. */
void TextSource::RemoveNewlinePadding(const StringFormat &format, RectF &box)
{
	RectF before;
	RectF after;
	Status stat;

	stat = graphics.MeasureString(L"W", 2, font.get(), PointF(0.0f, 0.0f),
			&format, &before);
	warn_stat("MeasureString (without newline)");

	stat = graphics.MeasureString(L"W\n", 3, font.get(), PointF(0.0f, 0.0f),
			&format, &after);
	warn_stat("MeasureString (with newline)");

	float offset_cx = after.Width - before.Width;
	float offset_cy = after.Height - before.Height;

	if (!vertical) {
		if (offset_cx >= 1.0f)
			offset_cx -= 1.0f;

		if (valign == VAlign::Center)
			box.Y -= offset_cy * 0.5f;
		else if (valign == VAlign::Bottom)
			box.Y -= offset_cy;
	} else {
		if (offset_cy >= 1.0f)
			offset_cy -= 1.0f;

		if (align == Align::Center)
			box.X -= offset_cx * 0.5f;
		else if (align == Align::Right)
			box.X -= offset_cx;
	}

	box.Width  -= offset_cx;
	box.Height -= offset_cy;
}

void TextSource::CalculateTextSizes(const StringFormat &format,
		RectF &bounding_box, SIZE &text_size)
{
	RectF layout_box;
	RectF temp_box;
	Status stat;

	if (!text.empty()) {
		if (use_extents && wrap) {
			layout_box.X = layout_box.Y = 0;
			layout_box.Width  = float(extents_cx);
			layout_box.Height = float(extents_cy);

			if (use_outline) {
				layout_box.Width  -= outline_size;
				layout_box.Height -= outline_size;
			}

			stat = graphics.MeasureString(text.c_str(),
					(int)text.size() + 1, font.get(),
					layout_box, &format,
					&bounding_box);
			warn_stat("MeasureString (wrapped)");

			temp_box = bounding_box;
		} else {
			stat = graphics.MeasureString(text.c_str(),
					(int)text.size() + 1, font.get(),
					PointF(0.0f, 0.0f), &format,
					&bounding_box);
			warn_stat("MeasureString (non-wrapped)");

			temp_box = bounding_box;

			bounding_box.X = 0.0f;
			bounding_box.Y = 0.0f;

			RemoveNewlinePadding(format, bounding_box);

			if (use_outline) {
				bounding_box.Width  += outline_size;
				bounding_box.Height += outline_size;
			}
		}
	}

	if (vertical) {
		if (bounding_box.Width < face_size) {
			text_size.cx = face_size;
			bounding_box.Width = float(face_size);
		} else {
			text_size.cx = LONG(bounding_box.Width + EPSILON);
		}

		text_size.cy = LONG(bounding_box.Height + EPSILON);
	} else {
		if (bounding_box.Height < face_size) {
			text_size.cy = face_size;
			bounding_box.Height = float(face_size);
		} else {
			text_size.cy = LONG(bounding_box.Height + EPSILON);
		}

		text_size.cx = LONG(bounding_box.Width + EPSILON);
	}

	if (use_extents) {
		text_size.cx = extents_cx;
		text_size.cy = extents_cy;
	}

	text_size.cx += text_size.cx % 2;
	text_size.cy += text_size.cy % 2;

	int64_t total_size = int64_t(text_size.cx) * int64_t(text_size.cy);

	/* GPUs typically have texture size limitations */
	clamp(text_size.cx, MIN_SIZE_CX, MAX_SIZE_CX);
	clamp(text_size.cy, MIN_SIZE_CY, MAX_SIZE_CY);

	/* avoid taking up too much VRAM */
	if (total_size > MAX_AREA) {
		if (text_size.cx > text_size.cy)
			text_size.cx = (LONG)MAX_AREA / text_size.cy;
		else
			text_size.cy = (LONG)MAX_AREA / text_size.cx;
	}

	/* the internal text-rendering bounding box for is reset to
	 * its internal value in case the texture gets cut off */
	bounding_box.Width  = temp_box.Width;
	bounding_box.Height = temp_box.Height;
}

void TextSource::RenderOutlineText(Graphics &graphics,
		const GraphicsPath &path,
		const Brush &brush)
{
	DWORD outline_rgba = calc_color(outline_color, outline_opacity);
	Status stat;

	Pen pen(Color(outline_rgba), outline_size);
	stat = pen.SetLineJoin(LineJoinRound);
	warn_stat("pen.SetLineJoin");

	stat = graphics.DrawPath(&pen, &path);
	warn_stat("graphics.DrawPath");

	stat = graphics.FillPath(&brush, &path);
	warn_stat("graphics.FillPath");
}

void TextSource::RenderText()
{
	StringFormat format(StringFormat::GenericTypographic());
	Status stat;

	RectF box;
	SIZE size;

	GetStringFormat(format);
	CalculateTextSizes(format, box, size);

	unique_ptr<uint8_t> bits(new uint8_t[size.cx * size.cy * 4]);
	Bitmap bitmap(size.cx, size.cy, 4 * size.cx, PixelFormat32bppARGB,
			bits.get());

	Graphics graphics_bitmap(&bitmap);
	LinearGradientBrush brush(RectF(0, 0, (float)size.cx, (float)size.cy),
			Color(calc_color(color, opacity)),
			Color(calc_color(color2, opacity2)),
			gradient_dir, 1);
	DWORD full_bk_color = bk_color & 0xFFFFFF;

	if (!text.empty() || use_extents)
		full_bk_color |= get_alpha_val(bk_opacity);

	if ((size.cx > box.Width || size.cy > box.Height) && !use_extents) {
		stat = graphics_bitmap.Clear(Color(0));
		warn_stat("graphics_bitmap.Clear");

		SolidBrush bk_brush = Color(full_bk_color);
		stat = graphics_bitmap.FillRectangle(&bk_brush, box);
		warn_stat("graphics_bitmap.FillRectangle");
	} else {
		stat = graphics_bitmap.Clear(Color(full_bk_color));
		warn_stat("graphics_bitmap.Clear");
	}

	graphics_bitmap.SetTextRenderingHint(TextRenderingHintAntiAlias);
	graphics_bitmap.SetCompositingMode(CompositingModeSourceOver);
	graphics_bitmap.SetSmoothingMode(SmoothingModeAntiAlias);

	if (!text.empty()) {
		if (use_outline) {
			box.Offset(outline_size / 2, outline_size / 2);

			FontFamily family;
			GraphicsPath path;

			font->GetFamily(&family);
			stat = path.AddString(text.c_str(), (int)text.size(),
					&family, font->GetStyle(),
					font->GetSize(), box, &format);
			warn_stat("path.AddString");

			RenderOutlineText(graphics_bitmap, path, brush);
		} else {
			stat = graphics_bitmap.DrawString(text.c_str(),
					(int)text.size(), font.get(),
					box, &format, &brush);
			warn_stat("graphics_bitmap.DrawString");
		}
	}

	if (!tex || (LONG)cx != size.cx || (LONG)cy != size.cy) {
		obs_enter_graphics();
		if (tex)
			gs_texture_destroy(tex);

		const uint8_t *data = (uint8_t*)bits.get();
		tex = gs_texture_create(size.cx, size.cy, GS_BGRA, 1, &data,
				GS_DYNAMIC);

		obs_leave_graphics();

		cx = (uint32_t)size.cx;
		cy = (uint32_t)size.cy;

	} else if (tex) {
		obs_enter_graphics();
		gs_texture_set_image(tex, bits.get(), size.cx * 4, false);
		obs_leave_graphics();
	}
}

const char *TextSource::GetMainString(const char *str)
{
	if (!str)
		return "";
	if (!chatlog_mode || !chatlog_lines)
		return str;

	int lines = chatlog_lines;
	size_t len = strlen(str);
	if (!len)
		return str;

	const char *temp = str + len;

	while(temp != str) {
		temp--;

		if (temp[0] == '\n' && temp[1] != 0) {
			if (!--lines)
				break;
		}
	}

	return *temp == '\n' ? temp + 1 : temp;
}

void TextSource::LoadFileText()
{
	BPtr<char> file_text = os_quick_read_utf8_file(file.c_str());
	text = to_wide(GetMainString(file_text));

	if (!text.empty() && text.back() != '\n')
		text.push_back('\n');
}

#define obs_data_get_uint32 (uint32_t)obs_data_get_int

inline void TextSource::Update(obs_data_t *s)
{
	const char *new_text   = obs_data_get_string(s, S_TEXT);
	obs_data_t *font_obj   = obs_data_get_obj(s, S_FONT);
	const char *align_str  = obs_data_get_string(s, S_ALIGN);
	const char *valign_str = obs_data_get_string(s, S_VALIGN);
	uint32_t new_color     = obs_data_get_uint32(s, S_COLOR);
	uint32_t new_opacity   = obs_data_get_uint32(s, S_OPACITY);
	bool gradient          = obs_data_get_bool(s, S_GRADIENT);
	uint32_t new_color2    = obs_data_get_uint32(s, S_GRADIENT_COLOR);
	uint32_t new_opacity2  = obs_data_get_uint32(s, S_GRADIENT_OPACITY);
	float new_grad_dir     = (float)obs_data_get_double(s, S_GRADIENT_DIR);
	bool new_vertical      = obs_data_get_bool(s, S_VERTICAL);
	bool new_outline       = obs_data_get_bool(s, S_OUTLINE);
	uint32_t new_o_color   = obs_data_get_uint32(s, S_OUTLINE_COLOR);
	uint32_t new_o_opacity = obs_data_get_uint32(s, S_OUTLINE_OPACITY);
	uint32_t new_o_size    = obs_data_get_uint32(s, S_OUTLINE_SIZE);
	bool new_use_file      = obs_data_get_bool(s, S_USE_FILE);
	bool new_use_song      = obs_data_get_bool(s, S_USE_SONG);
	bool new_use_vnr       = obs_data_get_bool(s, S_USE_VNR);
#if VNR_kyob1010_MultipleStream
	vnr_mode               = obs_data_get_string(s, S_VNR_MODE);
#endif
	const char *new_file   = obs_data_get_string(s, S_FILE);
	bool new_chat_mode     = obs_data_get_bool(s, S_CHATLOG_MODE);
	int new_chat_lines     = (int)obs_data_get_int(s, S_CHATLOG_LINES);
	bool new_extents       = obs_data_get_bool(s, S_EXTENTS);
	bool new_extents_wrap  = obs_data_get_bool(s, S_EXTENTS_WRAP);
	uint32_t n_extents_cx  = obs_data_get_uint32(s, S_EXTENTS_CX);
	uint32_t n_extents_cy  = obs_data_get_uint32(s, S_EXTENTS_CY);

	const char *font_face  = obs_data_get_string(font_obj, "face");
	int font_size          = (int)obs_data_get_int(font_obj, "size");
	int64_t font_flags     = obs_data_get_int(font_obj, "flags");
	bool new_bold          = (font_flags & OBS_FONT_BOLD) != 0;
	bool new_italic        = (font_flags & OBS_FONT_ITALIC) != 0;
	bool new_underline     = (font_flags & OBS_FONT_UNDERLINE) != 0;
	bool new_strikeout     = (font_flags & OBS_FONT_STRIKEOUT) != 0;

	uint32_t new_bk_color   = obs_data_get_uint32(s, S_BKCOLOR);
	uint32_t new_bk_opacity = obs_data_get_uint32(s, S_BKOPACITY);

	/* ----------------------------- */

	wstring new_face = to_wide(font_face);

	if (new_face      != face      ||
	    face_size     != font_size ||
	    new_bold      != bold      ||
	    new_italic    != italic    ||
	    new_underline != underline ||
	    new_strikeout != strikeout) {

		face = new_face;
		face_size = font_size;
		bold = new_bold;
		italic = new_italic;
		underline = new_underline;
		strikeout = new_strikeout;

		UpdateFont();
	}

	/* ----------------------------- */

	new_color = rgb_to_bgr(new_color);
	new_color2 = rgb_to_bgr(new_color2);
	new_o_color = rgb_to_bgr(new_o_color);
	new_bk_color = rgb_to_bgr(new_bk_color);

	color = new_color;
	opacity = new_opacity;
	color2 = new_color2;
	opacity2 = new_opacity2;
	gradient_dir = new_grad_dir;
	vertical = new_vertical;

	bk_color = new_bk_color;
	bk_opacity = new_bk_opacity;
	use_extents = new_extents;
	wrap = new_extents_wrap;
	extents_cx = n_extents_cx;
	extents_cy = n_extents_cy;

	if (!gradient) {
		color2 = color;
		opacity2 = opacity;
	}

	chatlog_mode = new_chat_mode;
	chatlog_lines = new_chat_lines;

	if (new_use_file) {
		mode = Mode::file;
		file = new_file;
		file_timestamp = get_modified_timestamp(new_file);
		LoadFileText();
	} else if (new_use_song) {
		mode = Mode::song;
		if (IsWindow(song_hwnd))
			get_song_name(song_hwnd);
	} else if (new_use_vnr) {
		mode = Mode::vnr;
		VNR_initial(s);
		if (mode == Mode::vnr)
			ReadFromVNR();
	} else {
		mode = Mode::text;
		text = to_wide(GetMainString(new_text));

		/* all text should end with newlines due to the fact that GDI+
		* treats strings without newlines differently in terms of
		* render size */
		if (!text.empty())
			text.push_back('\n');
	}
	if (!new_use_vnr && last_use_vnr) {
		--TextSource::vnr_count;
		last_use_vnr = false;
		TextSource::CloseSHM();
	}

	use_outline = new_outline;
	outline_color = new_o_color;
	outline_opacity = new_o_opacity;
	outline_size = roundf(float(new_o_size));

	if (strcmp(align_str, S_ALIGN_CENTER) == 0)
		align = Align::Center;
	else if (strcmp(align_str, S_ALIGN_RIGHT) == 0)
		align = Align::Right;
	else
		align = Align::Left;

	if (strcmp(valign_str, S_VALIGN_CENTER) == 0)
		valign = VAlign::Center;
	else if (strcmp(valign_str, S_VALIGN_BOTTOM) == 0)
		valign = VAlign::Bottom;
	else
		valign = VAlign::Top;

	RenderText();
	update_time_elapsed = 0.0f;

	/* ----------------------------- */

	obs_data_release(font_obj);
}

inline void TextSource::Tick(float seconds)
{
	if (!obs_source_showing(source))
		return;
	
	switch (mode) {
	case Mode::file :
		update_time_elapsed += seconds;
		if (update_time_elapsed >= 1.0f) {
			update_time_elapsed = 0.0f;
			time_t t = get_modified_timestamp(file.c_str());

			if (update_file) {
				LoadFileText();
				RenderText();
				update_file = false;
			}

			if (file_timestamp != t) {
				file_timestamp = t;
				update_file = true;
			}
		}
		break;
	case Mode::song :
		update_time_elapsed += seconds;
		if (update_time_elapsed >= 1.0f) {
			update_time_elapsed = 0.0f;
			if (!IsWindow(song_hwnd) || !get_song_name(song_hwnd)) {
				::EnumWindows(&TextSource::find_target
					, reinterpret_cast<LPARAM>(this));
			}
			// compiler bug? no BOOL = error, will lambda object be created every run?
			//::EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL
			//{
			//	TextSource* _TextSource = reinterpret_cast<TextSource *>(lParam);
			//	return !_TextSource->get_song_name(hwnd);
			//}
			//, reinterpret_cast<LPARAM>(this));
		}
		break;
	case Mode::vnr :
		ReadFromVNR();
		break;
	default:
		break;
	//	__assume(0); // https://docs.microsoft.com/en-us/cpp/intrinsics/assume
	}
}

inline void TextSource::Render(gs_effect_t *effect)
{
	if (!tex)
		return;

	gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), tex);
	gs_draw_sprite(tex, 0, cx, cy);
}

BOOL TextSource::get_song_name(HWND hwnd)
{
	wchar_t *title;
	int len;
	len = GetWindowTextLengthW(hwnd);
	if (!len)
		return FALSE;
	title = reinterpret_cast<wchar_t *>( malloc(sizeof(wchar_t) * (len + 1)) );
	if (!title || !GetWindowTextW(hwnd, title, len + 1)) {
		free(title);
		return FALSE;
	}
	if (song_pfunc) {
		bool result = (this->*song_pfunc)(title);
		free(title);
		if (!result)
			song_pfunc = nullptr;
		return result;
	}

	bool ok = get_song_browser_player(title,
			&TextSource::get_song_browser_youtube) ||
			get_song_foobar2000(title) ||
			get_song_osu(title);
	free(title);

	if (ok)
		song_hwnd = hwnd;
	return ok;
}

inline bool TextSource::get_song_browser_player(const wchar_t * const title, song_pfn const pfn)
{
	for (auto &brower : TextSource::browsers) {
		if ((wcsstr(title, brower) != NULL) && (this->*pfn)(title))
			return true;
	}
	return false;
}

bool TextSource::get_song_browser_youtube(const wchar_t * const title)
{
	wchar_t *strEnd = const_cast<wchar_t *>(wcsstr(title, L"- YouTube"));
	if (strEnd != nullptr) {
		*(strEnd - 1) = '\0'; // remove 1 space before strEnd
		set_song_name(title);
		song_pfunc = &TextSource::get_song_browser_youtube;
		return true;
	}
	return false;
}

bool TextSource::get_song_foobar2000(const wchar_t * const title)
{
	wchar_t *strEnd = const_cast<wchar_t *>(wcsstr(title, L"[foobar2000 v"));
	if (strEnd != nullptr) {
		*(strEnd - 1) = '\0'; // remove 1 space before strEnd
		set_song_name(title);
		song_pfunc = &TextSource::get_song_foobar2000;
		return true;
	}
	return false;
}

bool TextSource::get_song_osu(const wchar_t * const title)
{
	static const wchar_t app[] = L"osu!  -";
	wchar_t *strStart = const_cast<wchar_t*>(wcsstr(title, app));
	if (strStart != nullptr) {
		// don't + 1 (including '\0'), 1 space after strStart is removed
		strStart += sizeof(app) / sizeof(*app);
		set_song_name(strStart);
		song_pfunc = &TextSource::get_song_osu;
		return true;
	}
	return false;
}

void TextSource::set_song_name(const wchar_t * const name)
{
	if (text.compare(name)) {
		text = name;
		text.push_back('\n');
		RenderText();
	}
}

inline void TextSource::VNR_initial(obs_data *s)
{
	if (TextSource::shm.id == nullptr) {
		bool createSHM = false;
		hMapFile = OpenFileMapping(
			FILE_MAP_ALL_ACCESS,   // read/write access
			FALSE,                 // do not inherit the name
			VNR_SHM);              // name of mapping object
		if (hMapFile == NULL) {
			hMapFile = CreateFileMapping(INVALID_HANDLE_VALUE,
				NULL, PAGE_READWRITE, 0, VNR_SHM_SIZE, VNR_SHM);
			if (hMapFile == NULL) {
				VNR_FallBackToText(s);
				return;
			}
			createSHM = true;
		}

		TextSource::shm.id = static_cast<unsigned char *>(
			MapViewOfFile(hMapFile,   // handle to map object
				FILE_MAP_ALL_ACCESS, // read/write permission
				0,
				0,
				VNR_SHM_SIZE));
		if (TextSource::shm.id == nullptr) {
			VNR_FallBackToText(s);
			TextSource::CloseSHM();
			return;
		}

		TextSource::hMutex = OpenMutex(
			SYNCHRONIZE,
			FALSE,
			VNR_SHM_MUTEX);
		if (hMutex == NULL) {
			hMutex = CreateMutex(NULL, false, VNR_SHM_MUTEX);
			if (hMutex == NULL) {
				VNR_FallBackToText(s);
				TextSource::CloseSHM();
				return;
			}
		}

		TextSource::shm.data = reinterpret_cast<wchar_t *>
			(TextSource::shm.id + 1);
		if (createSHM) {
			// This happens rarely, no crash no need to lock
			// start game and OBS at the same time is prone to error
			//WaitForSingleObject(TextSource::hMutex, 5000);

			// set 3 bytes to 0 (actual 4)
			*reinterpret_cast<int *>(TextSource::shm.id) = 0;
			//ReleaseMutex(TextSource::hMutex);
		}
	}
	if (!last_use_vnr) {
		++TextSource::vnr_count;
		last_use_vnr = true;
	}
}

void TextSource::VNR_FallBackToText(obs_data *s)
{
	obs_data_set_bool(s, S_USE_VNR, false);
	last_use_vnr = false;
	mode = Mode::text;
}

BOOL CALLBACK TextSource::find_target(HWND hwnd, LPARAM lParam)
{
	TextSource* _TextSource = reinterpret_cast<TextSource *>(lParam);
	return !_TextSource->get_song_name(hwnd);
}

void TextSource::CloseSHM()
{
	if (TextSource::vnr_count != 0)
		return;
	if (TextSource::hMutex) {
		CloseHandle(TextSource::hMutex);
		TextSource::hMutex = NULL;
	}
	if (TextSource::shm.id) {
		UnmapViewOfFile(TextSource::shm.id);
		TextSource::shm.id = nullptr;
	}
	if (TextSource::hMapFile) {
		CloseHandle(TextSource::hMapFile);
		TextSource::hMapFile = NULL;
	}
}

void TextSource::ReadFromVNR()
{
#if VNR_kyob1010_MultipleStream
	switch (*vnr_mode) {
	case 'o':
		break;
	case 't':
		break;
	case 'b':
	default:
		break;
	}
#endif
	WaitForSingleObject(TextSource::hMutex, 5000);
	if (vnr_id != *TextSource::shm.id) {
		vnr_id = *TextSource::shm.id;
		text = TextSource::shm.data;
		// text always not empty? better let vnr add '\n'
		//text.push_back('\n');
		RenderText();
	}
	ReleaseMutex(TextSource::hMutex);
}


/* ------------------------------------------------------------------------- */

static ULONG_PTR gdip_token = 0;

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-text", "en-US")

#define set_vis(var, val, show) \
	do { \
		p = obs_properties_get(props, val); \
		obs_property_set_visible(p, var == show); \
	} while (false)

static bool use_file_changed(obs_properties_t *props, obs_property_t *p,
		obs_data_t *s)
{
	bool use_file = obs_data_get_bool(s, S_USE_FILE);
	if (use_file) {
		obs_data_set_bool(s, S_USE_SONG, false);
		obs_data_set_bool(s, S_USE_VNR, false);
#if VNR_kyob1010_MultipleStream
		p = obs_properties_get(props, S_VNR_MODE);
		obs_property_set_visible(p, false);
#endif
	}
	set_vis(use_file, S_TEXT, false);
	set_vis(use_file, S_FILE, true);
	return true;
}

static bool use_song_changed(obs_properties_t *props, obs_property_t *p,
		obs_data_t *s)
{
	bool use_song = obs_data_get_bool(s, S_USE_SONG);
	if (use_song) {
		obs_data_set_bool(s, S_USE_FILE, false);
		obs_data_set_bool(s, S_USE_VNR, false);
		p = obs_properties_get(props, S_FILE);
		obs_property_set_visible(p, false);
#if VNR_kyob1010_MultipleStream
		p = obs_properties_get(props, S_VNR_MODE);
		obs_property_set_visible(p, false);
#endif
	}

	set_vis(use_song, S_TEXT, false);
	return true;
}

static bool use_vnr_changed(obs_properties_t *props, obs_property_t *p,
	obs_data_t *s)
{
	bool use_vnr = obs_data_get_bool(s, S_USE_VNR);
	if (use_vnr) {
		obs_data_set_bool(s, S_USE_FILE, false);
		obs_data_set_bool(s, S_USE_SONG, false);
		p = obs_properties_get(props, S_FILE);
		obs_property_set_visible(p, false);
	}

	set_vis(use_vnr, S_TEXT, false);
#if VNR_kyob1010_MultipleStream
	set_vis(use_vnr, S_VNR_MODE, true);
#endif
	return true;
}

static bool outline_changed(obs_properties_t *props, obs_property_t *p,
		obs_data_t *s)
{
	bool outline = obs_data_get_bool(s, S_OUTLINE);

	set_vis(outline, S_OUTLINE_SIZE, true);
	set_vis(outline, S_OUTLINE_COLOR, true);
	set_vis(outline, S_OUTLINE_OPACITY, true);
	return true;
}

static bool chatlog_mode_changed(obs_properties_t *props, obs_property_t *p,
		obs_data_t *s)
{
	bool chatlog_mode = obs_data_get_bool(s, S_CHATLOG_MODE);

	set_vis(chatlog_mode, S_CHATLOG_LINES, true);
	return true;
}

static bool gradient_changed(obs_properties_t *props, obs_property_t *p,
		obs_data_t *s)
{
	bool gradient = obs_data_get_bool(s, S_GRADIENT);

	set_vis(gradient, S_GRADIENT_COLOR, true);
	set_vis(gradient, S_GRADIENT_OPACITY, true);
	set_vis(gradient, S_GRADIENT_DIR, true);
	return true;
}

static bool extents_modified(obs_properties_t *props, obs_property_t *p,
		obs_data_t *s)
{
	bool use_extents = obs_data_get_bool(s, S_EXTENTS);

	set_vis(use_extents, S_EXTENTS_WRAP, true);
	set_vis(use_extents, S_EXTENTS_CX, true);
	set_vis(use_extents, S_EXTENTS_CY, true);
	return true;
}

#undef set_vis

static obs_properties_t *get_properties(void *data)
{
	TextSource *s = reinterpret_cast<TextSource*>(data);
	string path;

	obs_properties_t *props = obs_properties_create();
	obs_property_t *p;

	obs_properties_add_font(props, S_FONT, T_FONT);

	p = obs_properties_add_bool(props, S_USE_FILE, T_USE_FILE);
	obs_property_set_modified_callback(p, use_file_changed);
	p = obs_properties_add_bool(props, S_USE_SONG, T_USE_SONG);
	obs_property_set_modified_callback(p, use_song_changed);
	p = obs_properties_add_bool(props, S_USE_VNR, T_USE_VNR);
	obs_property_set_modified_callback(p, use_vnr_changed);
#if VNR_kyob1010_MultipleStream
	p = obs_properties_add_list(props, S_VNR_MODE, T_VNR_MODE,
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, "original", "o");
	obs_property_list_add_string(p, "translation", "t");
	obs_property_list_add_string(p, "original + translation", "b");
#endif

	string filter;
	filter += T_FILTER_TEXT_FILES;
	filter += " (*.txt);;";
	filter += T_FILTER_ALL_FILES;
	filter += " (*.*)";

	if (s && !s->file.empty()) {
		const char *slash;

		path = s->file;
		replace(path.begin(), path.end(), '\\', '/');
		slash = strrchr(path.c_str(), '/');
		if (slash)
			path.resize(slash - path.c_str() + 1);
	}

	obs_properties_add_text(props, S_TEXT, T_TEXT, OBS_TEXT_MULTILINE);
	obs_properties_add_path(props, S_FILE, T_FILE, OBS_PATH_FILE,
			filter.c_str(), path.c_str());

	obs_properties_add_bool(props, S_VERTICAL, T_VERTICAL);
	obs_properties_add_color(props, S_COLOR, T_COLOR);
	obs_properties_add_int_slider(props, S_OPACITY, T_OPACITY, 0, 100, 1);

	p = obs_properties_add_bool(props, S_GRADIENT, T_GRADIENT);
	obs_property_set_modified_callback(p, gradient_changed);

	obs_properties_add_color(props, S_GRADIENT_COLOR, T_GRADIENT_COLOR);
	obs_properties_add_int_slider(props, S_GRADIENT_OPACITY,
			T_GRADIENT_OPACITY, 0, 100, 1);
	obs_properties_add_float_slider(props, S_GRADIENT_DIR,
			T_GRADIENT_DIR, 0, 360, 0.1);
	
	obs_properties_add_color(props, S_BKCOLOR, T_BKCOLOR);
	obs_properties_add_int_slider(props, S_BKOPACITY, T_BKOPACITY,
			0, 100, 1);

	p = obs_properties_add_list(props, S_ALIGN, T_ALIGN,
			OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, T_ALIGN_LEFT,   S_ALIGN_LEFT);
	obs_property_list_add_string(p, T_ALIGN_CENTER, S_ALIGN_CENTER);
	obs_property_list_add_string(p, T_ALIGN_RIGHT,  S_ALIGN_RIGHT);

	p = obs_properties_add_list(props, S_VALIGN, T_VALIGN,
			OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, T_VALIGN_TOP,    S_VALIGN_TOP);
	obs_property_list_add_string(p, T_VALIGN_CENTER, S_VALIGN_CENTER);
	obs_property_list_add_string(p, T_VALIGN_BOTTOM, S_VALIGN_BOTTOM);

	p = obs_properties_add_bool(props, S_OUTLINE, T_OUTLINE);
	obs_property_set_modified_callback(p, outline_changed);

	obs_properties_add_int(props, S_OUTLINE_SIZE, T_OUTLINE_SIZE, 1, 20, 1);
	obs_properties_add_color(props, S_OUTLINE_COLOR, T_OUTLINE_COLOR);
	obs_properties_add_int_slider(props, S_OUTLINE_OPACITY,
			T_OUTLINE_OPACITY, 0, 100, 1);

	p = obs_properties_add_bool(props, S_CHATLOG_MODE, T_CHATLOG_MODE);
	obs_property_set_modified_callback(p, chatlog_mode_changed);

	obs_properties_add_int(props, S_CHATLOG_LINES, T_CHATLOG_LINES,
			1, 1000, 1);

	p = obs_properties_add_bool(props, S_EXTENTS, T_EXTENTS);
	obs_property_set_modified_callback(p, extents_modified);

	obs_properties_add_int(props, S_EXTENTS_CX, T_EXTENTS_CX, 32, 8000, 1);
	obs_properties_add_int(props, S_EXTENTS_CY, T_EXTENTS_CY, 32, 8000, 1);
	obs_properties_add_bool(props, S_EXTENTS_WRAP, T_EXTENTS_WRAP);

	return props;
}

bool obs_module_load(void)
{
	obs_source_info si = {};
	si.id = "text_gdiplus";
	si.type = OBS_SOURCE_TYPE_INPUT;
	si.output_flags = OBS_SOURCE_VIDEO;
	si.get_properties = get_properties;

	si.get_name = [] (void*)
	{
		return obs_module_text("TextGDIPlus");
	};
	si.create = [] (obs_data_t *settings, obs_source_t *source)
	{
		return (void*)new TextSource(source, settings);
	};
	si.destroy = [] (void *data)
	{
		delete reinterpret_cast<TextSource*>(data);
	};
	si.get_width = [] (void *data)
	{
		return reinterpret_cast<TextSource*>(data)->cx;
	};
	si.get_height = [] (void *data)
	{
		return reinterpret_cast<TextSource*>(data)->cy;
	};
	si.get_defaults = [] (obs_data_t *settings)
	{
		obs_data_t *font_obj = obs_data_create();
		obs_data_set_default_string(font_obj, "face", "Arial");
		obs_data_set_default_int(font_obj, "size", 36);

		obs_data_set_default_obj(settings, S_FONT, font_obj);
#if VNR_kyob1010_MultipleStream
		obs_data_set_default_string(settings, S_VNR_MODE, "t");
#endif
		obs_data_set_default_string(settings, S_ALIGN, S_ALIGN_LEFT);
		obs_data_set_default_string(settings, S_VALIGN, S_VALIGN_TOP);
		obs_data_set_default_int(settings, S_COLOR, 0xFFFFFF);
		obs_data_set_default_int(settings, S_OPACITY, 100);
		obs_data_set_default_int(settings, S_GRADIENT_COLOR, 0xFFFFFF);
		obs_data_set_default_int(settings, S_GRADIENT_OPACITY, 100);
		obs_data_set_default_double(settings, S_GRADIENT_DIR, 90.0);
		obs_data_set_default_int(settings, S_BKCOLOR, 0x000000);
		obs_data_set_default_int(settings, S_BKOPACITY, 0);
		obs_data_set_default_int(settings, S_OUTLINE_SIZE, 2);
		obs_data_set_default_int(settings, S_OUTLINE_COLOR, 0xFFFFFF);
		obs_data_set_default_int(settings, S_OUTLINE_OPACITY, 100);
		obs_data_set_default_int(settings, S_CHATLOG_LINES, 6);
		obs_data_set_default_bool(settings, S_EXTENTS_WRAP, true);
		obs_data_set_default_int(settings, S_EXTENTS_CX, 100);
		obs_data_set_default_int(settings, S_EXTENTS_CY, 100);

		obs_data_release(font_obj);
	};
	si.update = [] (void *data, obs_data_t *settings)
	{
		reinterpret_cast<TextSource*>(data)->Update(settings);
	};
	si.video_tick = [] (void *data, float seconds)
	{
		reinterpret_cast<TextSource*>(data)->Tick(seconds);
	};
	si.video_render = [] (void *data, gs_effect_t *effect)
	{
		reinterpret_cast<TextSource*>(data)->Render(effect);
	};

	obs_register_source(&si);

	const GdiplusStartupInput gdip_input;
	GdiplusStartup(&gdip_token, &gdip_input, nullptr);
	return true;
}

void obs_module_unload(void)
{
	GdiplusShutdown(gdip_token);
}
