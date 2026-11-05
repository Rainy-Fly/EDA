#include "CardPanel.hpp"
#include "Theme.hpp"
#include "ThemedButton.hpp"

#include <wx/dcbuffer.h>
#include <wx/dc.h>
#include <wx/graphics.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <algorithm>
#include <utility>

CardPanel::CardPanel(wxWindow* parent, wxWindowID id, const wxString& title,
                     bool closeable, int card_margin)
    : wxPanel(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE),
      title_(title), closeable_(closeable){
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(UiTheme::Surface);
    card_margin_ = card_margin >= 0 ? card_margin : FromDIP(UiTheme::SpaceXs);
    content_padding_ = FromDIP(10);

    root_sizer_ = new wxBoxSizer(wxVERTICAL);

    if(closeable_ || !title_.empty()){
        header_sizer_ = new wxBoxSizer(wxHORIZONTAL);
        title_text_ = new wxStaticText(this, wxID_ANY, title_);
        wxFont title_font = title_text_->GetFont();
        title_font.SetWeight(wxFONTWEIGHT_BOLD);
        title_font.SetPointSize(title_font.GetPointSize() + 1);
        title_text_->SetFont(title_font);
        title_text_->SetForegroundColour(UiTheme::Brand900);
        title_text_->SetBackgroundColour(UiTheme::Surface);
        header_sizer_->Add(title_text_, 1, wxALIGN_CENTER_VERTICAL);

        if(closeable_){
            close_button_ = new ThemedButton(this, wxID_ANY,
                                             wxString::FromUTF8("×"), false);
            close_button_->SetFlat(true);
            close_button_->SetMinSize(wxSize(FromDIP(26), FromDIP(26)));
            close_button_->SetToolTip(wxString::FromUTF8("关闭面板"));
            close_button_->SetClickHandler([this]{
                if(close_handler_) close_handler_();
            });
            header_sizer_->Add(close_button_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT,
                               FromDIP(UiTheme::SpaceXs));
        }

        root_sizer_->Add(header_sizer_, 0,
                         wxEXPAND | wxLEFT | wxRIGHT | wxTOP,
                         card_margin_ + content_padding_);
    }

    body_sizer_ = new wxBoxSizer(wxVERTICAL);
    root_sizer_->Add(body_sizer_, 1, wxEXPAND | wxALL,
                     card_margin_ + content_padding_);

    SetSizer(root_sizer_);
    Bind(wxEVT_PAINT, &CardPanel::OnPaint, this);
    Bind(wxEVT_SIZE, &CardPanel::OnSize, this);
}

void CardPanel::SetTitle(const wxString& title){
    title_ = title;
    if(title_text_){
        title_text_->SetLabel(title);
    }
    Refresh();
}

void CardPanel::SetCloseHandler(std::function<void()> handler){
    close_handler_ = std::move(handler);
    if(close_button_){
        close_button_->SetClickHandler([this]{
            if(close_handler_) close_handler_();
        });
    }
}

void CardPanel::SetCardMargin(int margin){
    // 构造时边距已写进 sizer flags；运行期调整只影响卡片绘制。
    // 现有调用方都在构造后立即设置，所以不需要重建 sizer item。
    card_margin_ = std::max(0, margin);
    Refresh();
}

void CardPanel::OnSize(wxSizeEvent& event){
    Refresh(false);
    event.Skip();
}

void CardPanel::OnPaint(wxPaintEvent&){
    wxAutoBufferedPaintDC dc(this);
    const wxSize size = GetClientSize();
    if(size.x <= 0 || size.y <= 0) return;

    dc.SetBackground(wxBrush(UiTheme::Page));
    dc.Clear();

    const wxRect card(card_margin_, card_margin_,
                      size.x - card_margin_ * 2,
                      size.y - card_margin_ * 2);
    if(card.width <= 0 || card.height <= 0) return;

    UiTheme::CardStyle style;
    style.fill = UiTheme::Surface;
    style.border = UiTheme::BorderSubtle;
    style.radius = FromDIP(UiTheme::RadiusCard);
    style.shadow = true;

    bool drawn = false;
    if(wxGraphicsContext* gc = wxGraphicsContext::CreateFromUnknownDC(dc)){
        UiTheme::DrawCard(*gc, card, style);
        delete gc;
        drawn = true;
    }
    if(!drawn){
        dc.SetPen(wxPen(style.border, FromDIP(1)));
        dc.SetBrush(wxBrush(style.fill));
        dc.DrawRoundedRectangle(card, style.radius);
    }
}
