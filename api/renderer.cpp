// Include automatically generated configuration file if running autoconf.
#ifdef HAVE_CONFIG_H
#include "config_auto.h"
#endif

#include <string>
#include "baseapi.h"
#include "genericvector.h"
#include "math.h"
#include "renderer.h"

namespace tesseract {

/** Max string length of an int.  */
const int kMaxIntSize = 22;

/**********************************************************************
 * Base Renderer interface implementation
 **********************************************************************/
TessResultRenderer::TessResultRenderer(const char *outputbase,
                                       const char* extension)
    : file_extension_(extension),
      title_(""), imagenum_(-1),
      fout_(stdout),
      next_(NULL),
      happy_(true) {
  if (strcmp(outputbase, "-") && strcmp(outputbase, "stdout")) {
    STRING outfile = STRING(outputbase) + STRING(".") + STRING(file_extension_);
    fout_ = fopen(outfile.string(), "wb");
    if (fout_ == NULL) {
      happy_ = false;
    }
  }
}

TessResultRenderer::~TessResultRenderer() {
 if (fout_ != stdout)
    fclose(fout_);
  else
    clearerr(fout_);
  delete next_;
}

void TessResultRenderer::insert(TessResultRenderer* next) {
  if (next == NULL) return;

  TessResultRenderer* remainder = next_;
  next_ = next;
  if (remainder) {
    while (next->next_ != NULL) {
      next = next->next_;
    }
    next->next_ = remainder;
  }
}

bool TessResultRenderer::BeginDocument(const char* title) {
  if (!happy_) return false;
  title_ = title;
  imagenum_ = -1;
  bool ok = BeginDocumentHandler();
  if (next_) {
    ok = next_->BeginDocument(title) && ok;
  }
  return ok;
}

bool TessResultRenderer::AddImage(TessBaseAPI* api) {
  if (!happy_) return false;
  ++imagenum_;
  bool ok = AddImageHandler(api);
  if (next_) {
    ok = next_->AddImage(api) && ok;
  }
  return ok;
}

bool TessResultRenderer::EndDocument() {
  if (!happy_) return false;
  bool ok = EndDocumentHandler();
  if (next_) {
    ok = next_->EndDocument() && ok;
  }
  return ok;
}

void TessResultRenderer::AppendString(const char* s) {
  AppendData(s, strlen(s));
}

void TessResultRenderer::AppendData(const char* s, int len) {
  int n = fwrite(s, 1, len, fout_);
  if (n != len) happy_ = false;
}

bool TessResultRenderer::BeginDocumentHandler() {
  return happy_;
}

bool TessResultRenderer::EndDocumentHandler() {
  return happy_;
}


/**********************************************************************
 * UTF8 Text Renderer interface implementation
 **********************************************************************/
TessTextRenderer::TessTextRenderer(const char *outputbase)
    : TessResultRenderer(outputbase, "txt") {
}

bool TessTextRenderer::AddImageHandler(TessBaseAPI* api) {
  char* utf8 = api->GetUTF8Text();
  if (utf8 == NULL) {
    return false;
  }

  AppendString(utf8);
  delete[] utf8;

  bool pageBreak = false;
  api->GetBoolVariable("include_page_breaks", &pageBreak);
  const char* pageSeparator = api->GetStringVariable("page_separator");
  if (pageBreak) {
    AppendString(pageSeparator);
  }

  return true;
}

/**********************************************************************
 * HOcr Text Renderer interface implementation
 **********************************************************************/
TessHOcrRenderer::TessHOcrRenderer(const char *outputbase)
    : TessResultRenderer(outputbase, "hocr") {
    font_info_ = false;
}

TessHOcrRenderer::TessHOcrRenderer(const char *outputbase, bool font_info)
    : TessResultRenderer(outputbase, "hocr") {
    font_info_ = font_info;
}

bool TessHOcrRenderer::BeginDocumentHandler() {
  AppendString(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\"\n"
        "    \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">\n"
        "<html xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"en\" "
        "lang=\"en\">\n <head>\n  <title>");
  AppendString(title());
  AppendString(
      "</title>\n"
      "<meta http-equiv=\"Content-Type\" content=\"text/html;"
      "charset=utf-8\" />\n"
      "  <meta name='ocr-system' content='tesseract " TESSERACT_VERSION_STR
              "' />\n"
      "  <meta name='ocr-capabilities' content='ocr_page ocr_carea ocr_par"
      " ocr_line ocrx_word");
  if (font_info_)
    AppendString(
      " ocrp_lang ocrp_dir ocrp_font ocrp_fsize ocrp_wconf");
  AppendString(
      "'/>\n"
      "</head>\n<body>\n");

  return true;
}

bool TessHOcrRenderer::EndDocumentHandler() {
  AppendString(" </body>\n</html>\n");

  return true;
}

bool TessHOcrRenderer::AddImageHandler(TessBaseAPI* api) {
  char* hocr = api->GetHOCRText(imagenum());
  if (hocr == NULL) return false;

  AppendString(hocr);
  delete[] hocr;

  return true;
}

/** Escape a char string - remove <>&"' with HTML codes. */
STRING HOcrEscape(const char* text) {
  STRING ret;
  const char *ptr;
  for (ptr = text; *ptr; ptr++) {
    switch (*ptr) {
      case '<': ret += "&lt;"; break;
      case '>': ret += "&gt;"; break;
      case '&': ret += "&amp;"; break;
      case '"': ret += "&quot;"; break;
      case '\'': ret += "&#39;"; break;
      default: ret += *ptr;
    }
  }
  return ret;
}

/**
 * Gets the block orientation at the current iterator position.
 */
static tesseract::Orientation GetBlockTextOrientation(const PageIterator *it) {
  tesseract::Orientation orientation;
  tesseract::WritingDirection writing_direction;
  tesseract::TextlineOrder textline_order;
  float deskew_angle;
  it->Orientation(&orientation, &writing_direction, &textline_order,
                  &deskew_angle);
  return orientation;
}

/**
 * Fits a line to the baseline at the given level, and appends its coefficients
 * to the hOCR string.
 * NOTE: The hOCR spec is unclear on how to specify baseline coefficients for
 * rotated textlines. For this reason, on textlines that are not upright, this
 * method currently only inserts a 'textangle' property to indicate the rotation
 * direction and does not add any baseline information to the hocr string.
 */
static void AddBaselineCoordsTohOCR(const PageIterator *it,
                                    PageIteratorLevel level,
                                    STRING* hocr_str) {
  tesseract::Orientation orientation = tesseract::GetBlockTextOrientation(it);
  if (orientation != ORIENTATION_PAGE_UP) {
    hocr_str->add_str_int("; textangle ", 360 - orientation * 90);
    return;
  }

  int left, top, right, bottom;
  it->BoundingBox(level, &left, &top, &right, &bottom);

  // Try to get the baseline coordinates at this level.
  int x1, y1, x2, y2;
  if (!it->Baseline(level, &x1, &y1, &x2, &y2))
    return;
  // Following the description of this field of the hOCR spec, we convert the
  // baseline coordinates so that "the bottom left of the bounding box is the
  // origin".
  x1 -= left;
  x2 -= left;
  y1 -= bottom;
  y2 -= bottom;

  // Now fit a line through the points so we can extract coefficients for the
  // equation:  y = p1 x + p0
  double p1 = 0;
  double p0 = 0;
  if (x1 == x2) {
    // Problem computing the polynomial coefficients.
    return;
  }
  p1 = (y2 - y1) / static_cast<double>(x2 - x1);
  p0 = y1 - static_cast<double>(p1 * x1);

  hocr_str->add_str_double("; baseline ", round(p1 * 1000.0) / 1000.0);
  hocr_str->add_str_double(" ", round(p0 * 1000.0) / 1000.0);
}

static void AddIdTohOCR(STRING* hocr_str, const std::string base, int num1, int num2) {
  unsigned long bufsize = base.length() + 2 * kMaxIntSize;
  char id_buffer[bufsize];
  if (num2 >= 0) {
    snprintf(id_buffer, bufsize - 1, "%s_%d_%d", base.c_str(), num1, num2);
  } else {
    snprintf(id_buffer, bufsize - 1, "%s_%d", base.c_str(), num1);
  }
  id_buffer[bufsize - 1] = '\0';
  *hocr_str += " id='";
  *hocr_str += id_buffer;
  *hocr_str += "'";
}

static void AddBoxTohOCR(const ResultIterator *it,
                         PageIteratorLevel level,
                         STRING* hocr_str) {
  int left, top, right, bottom;
  it->BoundingBox(level, &left, &top, &right, &bottom);
  // This is the only place we use double quotes instead of single quotes,
  // but it may too late to change for consistency
  hocr_str->add_str_int(" title=\"bbox ", left);
  hocr_str->add_str_int(" ", top);
  hocr_str->add_str_int(" ", right);
  hocr_str->add_str_int(" ", bottom);
  // Add baseline coordinates & heights for textlines only.
  if (level == RIL_TEXTLINE) {
    AddBaselineCoordsTohOCR(it, level, hocr_str);
    // add custom height measures
    float row_height, descenders, ascenders;  // row attributes
    it->RowAttributes(&row_height, &descenders, &ascenders);
    // TODO: Do we want to limit these to a single decimal place?
    hocr_str->add_str_double("; x_size ", row_height);
    hocr_str->add_str_double("; x_descenders ", descenders * -1);
    hocr_str->add_str_double("; x_ascenders ", ascenders);
  }
  *hocr_str += "\">";
}

/**
 * Make a HTML-formatted string with hOCR markup from the internal
 * data structures.
 * page_number is 0-based but will appear in the output as 1-based.
 * Image name/input_file_ can be set by SetInputName before calling
 * GetHOCRText
 * STL removed from original patch submission and refactored by rays.
 */
char* TessBaseAPI::GetHOCRText(int page_number) {
  return GetHOCRText(NULL,page_number);
}

/**
 * Make a HTML-formatted string with hOCR markup from the internal
 * data structures.
 * page_number is 0-based but will appear in the output as 1-based.
 * Image name/input_file_ can be set by SetInputName before calling
 * GetHOCRText
 * STL removed from original patch submission and refactored by rays.
 */
char* TessBaseAPI::GetHOCRText(struct ETEXT_DESC* monitor, int page_number) {
  if (tesseract_ == NULL ||
      (page_res_ == NULL && Recognize(monitor) < 0))
    return NULL;

  int lcnt = 1, bcnt = 1, pcnt = 1, wcnt = 1;
  int page_id = page_number + 1;  // hOCR uses 1-based page numbers.
  bool para_is_ltr = true; // Default direction is LTR
  const char* paragraph_lang = NULL;
  bool font_info = false;
  GetBoolVariable("hocr_font_info", &font_info);

  STRING hocr_str("");

  if (input_file_ == NULL)
      SetInputName(NULL);

#ifdef _WIN32
  // convert input name from ANSI encoding to utf-8
  int str16_len = MultiByteToWideChar(CP_ACP, 0, input_file_->string(), -1,
                                      NULL, 0);
  wchar_t *uni16_str = new WCHAR[str16_len];
  str16_len = MultiByteToWideChar(CP_ACP, 0, input_file_->string(), -1,
                                  uni16_str, str16_len);
  int utf8_len = WideCharToMultiByte(CP_UTF8, 0, uni16_str, str16_len, NULL,
                                     0, NULL, NULL);
  char *utf8_str = new char[utf8_len];
  WideCharToMultiByte(CP_UTF8, 0, uni16_str, str16_len, utf8_str,
                      utf8_len, NULL, NULL);
  *input_file_ = utf8_str;
  delete[] uni16_str;
  delete[] utf8_str;
#endif

  hocr_str += "  <div class='ocr_page'";
  AddIdTohOCR(&hocr_str, "page", page_id, -1);
  hocr_str += " title='image \"";
  if (input_file_) {
    hocr_str += HOcrEscape(input_file_->string());
  } else {
    hocr_str += "unknown";
  }
  hocr_str.add_str_int("\"; bbox ", rect_left_);
  hocr_str.add_str_int(" ", rect_top_);
  hocr_str.add_str_int(" ", rect_width_);
  hocr_str.add_str_int(" ", rect_height_);
  hocr_str.add_str_int("; ppageno ", page_number);
  hocr_str += "'>\n";

  ResultIterator *res_it = GetIterator();
  while (!res_it->Empty(RIL_BLOCK)) {
    if (res_it->Empty(RIL_WORD)) {
      res_it->Next(RIL_WORD);
      continue;
    }

    // Open any new block/paragraph/textline.
    if (res_it->IsAtBeginningOf(RIL_BLOCK)) {
      para_is_ltr = true; // reset to default direction
      hocr_str += "   <div class='ocr_carea'";
      AddIdTohOCR(&hocr_str, "block", page_id, bcnt);
      AddBoxTohOCR(res_it, RIL_BLOCK, &hocr_str);
    }
    if (res_it->IsAtBeginningOf(RIL_PARA)) {
      hocr_str += "\n    <p class='ocr_par'";
      if (res_it->ParagraphIsLtr()) {
        para_is_ltr = true;
        hocr_str += " dir='ltr'";
      } else {
        hocr_str += " dir='rtl'";
      }
      AddIdTohOCR(&hocr_str, "par", page_id, pcnt);
      paragraph_lang = res_it->WordRecognitionLanguage();
      if (paragraph_lang) {
          hocr_str += "'"; // it's messed up that quotes are asymmetrical
          hocr_str += " lang='";
          hocr_str += paragraph_lang;
      }
      AddBoxTohOCR(res_it, RIL_PARA, &hocr_str);
    }
    if (res_it->IsAtBeginningOf(RIL_TEXTLINE)) {
      hocr_str += "\n     <span class='ocr_line'";
      AddIdTohOCR(&hocr_str, "line", page_id, lcnt);
      AddBoxTohOCR(res_it, RIL_TEXTLINE, &hocr_str);
    }

    // Now, process the word...
    hocr_str += "<span class='ocrx_word'";
    AddIdTohOCR(&hocr_str, "word", page_id, wcnt);
    int left, top, right, bottom;
    bool bold, italic, underlined, monospace, serif, smallcaps;
    int pointsize, font_id;
    const char *font_name;
    res_it->BoundingBox(RIL_WORD, &left, &top, &right, &bottom);
    font_name = res_it->WordFontAttributes(&bold, &italic, &underlined,
                                           &monospace, &serif, &smallcaps,
                                           &pointsize, &font_id);
    hocr_str.add_str_int(" title='bbox ", left);
    hocr_str.add_str_int(" ", top);
    hocr_str.add_str_int(" ", right);
    hocr_str.add_str_int(" ", bottom);
    hocr_str.add_str_int("; x_wconf ", res_it->Confidence(RIL_WORD));
    if (font_info) {
      if (font_name) {
        hocr_str += "; x_font ";
        hocr_str += HOcrEscape(font_name);
      }
      hocr_str.add_str_int("; x_fsize ", pointsize);
    }
    hocr_str += "'";
    const char* lang = res_it->WordRecognitionLanguage();
    if (lang && (!paragraph_lang || strcmp(lang, paragraph_lang))) {
      hocr_str += " lang='";
      hocr_str += lang;
      hocr_str += "'";
    }
    switch (res_it->WordDirection()) {
      // Only emit direction if different from current paragraph direction
      case DIR_LEFT_TO_RIGHT: if (!para_is_ltr) hocr_str += " dir='ltr'"; break;
      case DIR_RIGHT_TO_LEFT: if (para_is_ltr) hocr_str += " dir='rtl'"; break;
      case DIR_MIX:
      case DIR_NEUTRAL:
      default:  // Do nothing.
        break;
    }
    hocr_str += ">";
    bool last_word_in_line = res_it->IsAtFinalElement(RIL_TEXTLINE, RIL_WORD);
    bool last_word_in_para = res_it->IsAtFinalElement(RIL_PARA, RIL_WORD);
    bool last_word_in_block = res_it->IsAtFinalElement(RIL_BLOCK, RIL_WORD);
    if (bold) hocr_str += "<strong>";
    if (italic) hocr_str += "<em>";
    do {
      const char *grapheme = res_it->GetUTF8Text(RIL_SYMBOL);
      if (grapheme && grapheme[0] != 0) {
        hocr_str += HOcrEscape(grapheme);
      }
      delete []grapheme;
      res_it->Next(RIL_SYMBOL);
    } while (!res_it->Empty(RIL_BLOCK) && !res_it->IsAtBeginningOf(RIL_WORD));
    if (italic) hocr_str += "</em>";
    if (bold) hocr_str += "</strong>";
    hocr_str += "</span> ";
    wcnt++;
    // Close any ending block/paragraph/textline.
    if (last_word_in_line) {
      hocr_str += "\n     </span>";
      lcnt++;
    }
    if (last_word_in_para) {
      hocr_str += "\n    </p>\n";
      pcnt++;
      para_is_ltr = true; // back to default direction
    }
    if (last_word_in_block) {
      hocr_str += "   </div>\n";
      bcnt++;
    }
  }
  hocr_str += "  </div>\n";

  char *ret = new char[hocr_str.length() + 1];
  strcpy(ret, hocr_str.string());
  delete res_it;
  return ret;
}



/**********************************************************************
 * UNLV Text Renderer interface implementation
 **********************************************************************/
TessUnlvRenderer::TessUnlvRenderer(const char *outputbase)
    : TessResultRenderer(outputbase, "unlv") {
}

bool TessUnlvRenderer::AddImageHandler(TessBaseAPI* api) {
  char* unlv = api->GetUNLVText();
  if (unlv == NULL) return false;

  AppendString(unlv);
  delete[] unlv;

  return true;
}

/**********************************************************************
 * BoxText Renderer interface implementation
 **********************************************************************/
TessBoxTextRenderer::TessBoxTextRenderer(const char *outputbase)
    : TessResultRenderer(outputbase, "box") {
}

bool TessBoxTextRenderer::AddImageHandler(TessBaseAPI* api) {
  char* text = api->GetBoxText(imagenum());
  if (text == NULL) return false;

  AppendString(text);
  delete[] text;

  return true;
}

/**********************************************************************
 * Osd Text Renderer interface implementation
 **********************************************************************/
TessOsdRenderer::TessOsdRenderer(const char* outputbase)
    : TessResultRenderer(outputbase, "osd") {
}

bool TessOsdRenderer::AddImageHandler(TessBaseAPI* api) {
  char* osd = api->GetOsdText(imagenum());
  if (osd == NULL) return false;

  AppendString(osd);
  delete[] osd;

  return true;
}

}  // namespace tesseract
