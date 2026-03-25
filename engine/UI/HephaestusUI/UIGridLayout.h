#pragma once

#include "UIElement.h"

namespace Hephaestus::HephaestusUI {

class UIGridLayout : public UIElement {
public:
    void SetColumns(int columns) { m_Columns = std::max(1, columns); }
    void SetHSpacing(float spacing) { m_HSpacing = spacing; }
    void SetVSpacing(float spacing) { m_VSpacing = spacing; }
    void SetCellAlignment(UIHAlignment hAlign, UIVAlignment vAlign) {
        m_CellHAlign = hAlign;
        m_CellVAlign = vAlign;
    }

    [[nodiscard]] int GetColumns() const { return m_Columns; }
    [[nodiscard]] float GetHSpacing() const { return m_HSpacing; }
    [[nodiscard]] float GetVSpacing() const { return m_VSpacing; }

protected:
    void OnUpdateLayout() override;

private:
    int m_Columns = 2;
    float m_HSpacing = 8.0f;
    float m_VSpacing = 8.0f;
    UIHAlignment m_CellHAlign = UIHAlignment::Stretch;
    UIVAlignment m_CellVAlign = UIVAlignment::Top;
};

} // namespace Hephaestus::HephaestusUI
