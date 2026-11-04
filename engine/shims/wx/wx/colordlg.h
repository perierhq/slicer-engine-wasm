// Minimal wxWidgets stand-in for compiling Orca's src/slic3r/Utils/ColorSpaceConvert.cpp without wx.
//
// libslic3r's FlushVolCalc.cpp calls RGB2HSV(), which Orca defines in the GUI library's
// ColorSpaceConvert.cpp (the native CLI links the GUI library, so it resolves there). The engine
// compiles that same file; besides the colour-space maths it has two wxColour <-> string helpers,
// which need only the small wxColour subset below. Nothing in the engine calls them.
// Used for that one source file only (see engine/cmake/Libslic3r.cmake).
#pragma once

class wxColour {
public:
    typedef unsigned char ChannelType;

    wxColour() = default;
    wxColour(ChannelType red, ChannelType green, ChannelType blue, ChannelType alpha = 255)
        : m_red(red), m_green(green), m_blue(blue), m_alpha(alpha) {}

    ChannelType Red() const { return m_red; }
    ChannelType Green() const { return m_green; }
    ChannelType Blue() const { return m_blue; }
    ChannelType Alpha() const { return m_alpha; }

private:
    ChannelType m_red = 0, m_green = 0, m_blue = 0, m_alpha = 255;
};
