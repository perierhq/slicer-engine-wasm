// Declaration-only stand-ins for the Open CASCADE (OCCT) types that libslic3r's Format/STEP.hpp
// names. Model.hpp includes Format/STEP.hpp, so almost every libslic3r source sees these types,
// but the browser engine does not build OCCT and does not import STEP: Format/STEP.cpp is replaced
// by engine/stubs/Format/STEP.cpp, which throws. Nothing here is functional; the declarations only
// have to be complete enough for STEP.hpp to compile.
//
// STEP.hpp includes the four OCCT headers with quotes from src/libslic3r/Format/, where they do not
// exist, so the compiler falls back to the -I path and finds the headers next to this one. Keep the
// signatures that STEP.hpp overrides (Message_ProgressIndicator::UserBreak/Show) in sync with it.
#pragma once

// STEP.hpp itself uses these without including them (OCCT's headers bring them in).
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

typedef bool   Standard_Boolean;
typedef double Standard_Real;
typedef int    Standard_Integer;

namespace opencascade {
// Stand-in for OCCT's intrusive smart pointer. Never owns anything in the browser engine.
template<class T> class handle {
public:
    handle() = default;
    T   *get() const { return m_ptr; }
    T   *operator->() const { return m_ptr; }
    bool IsNull() const { return m_ptr == nullptr; }
    void Nullify() { m_ptr = nullptr; }
    explicit operator bool() const { return m_ptr != nullptr; }

private:
    T *m_ptr = nullptr;
};
} // namespace opencascade

#ifndef Handle
#define Handle(Class) opencascade::handle<Class>
#endif

class TopoDS_Shape {};
class TDocStd_Document;
class Message_ProgressScope {};

class XCAFApp_Application {
public:
    static Handle(XCAFApp_Application) GetApplication() { return {}; }
};

class XCAFDoc_ShapeTool {};
class XCAFDoc_DocumentTool {};

class Message_ProgressIndicator {
public:
    virtual ~Message_ProgressIndicator() = default;
    virtual Standard_Boolean UserBreak() { return false; }
    virtual void             Show(const Message_ProgressScope &, const Standard_Boolean) {}
    Standard_Real            GetPosition() const { return 0.; }
};
