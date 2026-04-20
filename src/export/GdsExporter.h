#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <cstdint>

class Design; // Forward declaration

class GdsExporter {
public:
    static bool exportGds(const std::string& filename, Design* design);

private:
    // ========= GDSII Record Types (Hex) =========
    // -- Library/Structure Framing --
    static const uint16_t HEADER    = 0x0002;
    static const uint16_t BGNLIB    = 0x0102;
    static const uint16_t LIBNAME   = 0x0206;
    static const uint16_t UNITS     = 0x0305;
    static const uint16_t ENDLIB    = 0x0400;
    static const uint16_t BGNSTR    = 0x0502;
    static const uint16_t STRNAME   = 0x0606;
    static const uint16_t ENDSTR    = 0x0700;

    // -- Geometry Records --
    static const uint16_t BOUNDARY  = 0x0800;
    static const uint16_t SREF      = 0x0A00;  // Structure Reference (Cell Instance)
    static const uint16_t LAYER     = 0x0D02;
    static const uint16_t DATATYPE  = 0x0E02;
    static const uint16_t SNAME     = 0x1206;  // Structure Name (for SREF)
    static const uint16_t STRANS    = 0x1A01;  // Transformation flags
    static const uint16_t XY        = 0x1003;
    static const uint16_t ENDEL     = 0x1100;

    // ========= GDS Layer Mapping (sky130 standard) =========
    // Router abstract layer → sky130 GDS layer number
    // sky130 stack:  li1(67) / met1(68) / met2(69) / met3(70) / met4(71) / met5(72)
    // Cut layers share GDS number with the metal above, datatype 44
    static const int GDS_CELL    = 10;   // Cell bounding box outlines (internal)
    static const int GDS_PIN     = 11;   // Pin access rectangles     (internal)
    static const int GDS_LI1     = 67;   // sky130 li1  — router layer 1
    static const int GDS_M1      = 68;   // sky130 met1 — router layer 2
    static const int GDS_M2      = 69;   // sky130 met2 — router layer 3
    static const int GDS_M3      = 70;   // sky130 met3 — router layer 4
    static const int GDS_M4      = 71;   // sky130 met4 — router layer 5
    static const int GDS_M5      = 72;   // sky130 met5 — router layer 6
    static const int GDS_VIA12   = 67;   // sky130 mcon  (li1→met1,  datatype 44)
    static const int GDS_VIA23   = 68;   // sky130 via   (met1→met2, datatype 44)
    static const int GDS_VIA34   = 69;   // sky130 via2  (met2→met3, datatype 44)
    static const int GDS_VIA45   = 70;   // sky130 via3  (met3→met4, datatype 44)
    static const int GDS_VIA56   = 71;   // sky130 via4  (met4→met5, datatype 44)

    // ========= Big-Endian Byte Swappers =========
    static uint16_t swap16(uint16_t val);
    static uint32_t swap32(uint32_t val);
    
    // GDSII 8-byte IBM System/360 floating point format
    static uint64_t real8ToGDS(double value);
    
    // ========= Binary Record Writers =========
    static void writeRecordHeader(std::ofstream& out, uint16_t size, uint16_t recordType);
    static void writeI2(std::ofstream& out, uint16_t recordType, int16_t val);
    static void writeI4(std::ofstream& out, uint16_t recordType, int32_t val);
    static void writeReal8(std::ofstream& out, uint16_t recordType, double val);
    static void writeString(std::ofstream& out, uint16_t recordType, const std::string& str);
    static void writeNoData(std::ofstream& out, uint16_t recordType);
    
    // ========= Geometry Writers =========
    static void writePolygon(std::ofstream& out, int layer, const std::vector<std::pair<int32_t, int32_t>>& points, int datatype = 0);
    static void writeWireRect(std::ofstream& out, int layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int half_width);
    static void writeSRef(std::ofstream& out, const std::string& structName, int32_t x, int32_t y);
    
    // ========= Layer Translation =========
    static int mapLayerToGds(int routerLayer);
    static int mapViaToGds(int fromLayer, int toLayer);
};
