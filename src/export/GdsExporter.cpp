#include "GdsExporter.h"
#include "db/Design.h"
#include "db/Library.h"
#include <iostream>
#include <cmath>
#include <cstring>
#include <set>
#include <algorithm>

// ========================================================================
//  BIG-ENDIAN BYTE SWAPPERS (x86 is Little-Endian, GDSII is Big-Endian)
// ========================================================================

uint16_t GdsExporter::swap16(uint16_t val) {
    return (val << 8) | (val >> 8);
}

uint32_t GdsExporter::swap32(uint32_t val) {
    return ((val << 24) & 0xFF000000) |
           ((val <<  8) & 0x00FF0000) |
           ((val >>  8) & 0x0000FF00) |
           ((val >> 24) & 0x000000FF);
}

// GDSII requires 8-byte IBM System/360 Hexadecimal floating-point format
uint64_t GdsExporter::real8ToGDS(double value) {
    if (value == 0.0) return 0;
    
    uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    
    int sign = (bits >> 63) & 1;
    int exp = ((bits >> 52) & 0x7FF) - 1023; // Unbias IEEE exponent
    uint64_t mantissa = (bits & 0xFFFFFFFFFFFFF) | 0x10000000000000; // Add hidden 1
    
    // Convert base-2 exponent to base-16
    exp += 4;
    int exp16 = (exp + 256) / 4; // Bias IBM Exponent by 64 (x4 = 256)
    int shift = (exp16 * 4) - exp - 4;
    
    mantissa >>= shift;
    
    uint64_t result = ((uint64_t)sign << 63) | ((uint64_t)(exp16 & 0x7F) << 56) | (mantissa >> 4);
    
    // Swap 64-bit endianness
    return ((result & 0xFF00000000000000ULL) >> 56) | 
           ((result & 0x00FF000000000000ULL) >> 40) | 
           ((result & 0x0000FF0000000000ULL) >> 24) | 
           ((result & 0x000000FF00000000ULL) >>  8) | 
           ((result & 0x00000000FF000000ULL) <<  8) | 
           ((result & 0x0000000000FF0000ULL) << 24) | 
           ((result & 0x000000000000FF00ULL) << 40) | 
           ((result & 0x00000000000000FFULL) << 56);
}

// ========================================================================
//  LOW-LEVEL BINARY RECORD WRITERS
// ========================================================================

void GdsExporter::writeRecordHeader(std::ofstream& out, uint16_t size, uint16_t recordType) {
    uint16_t swappedSize = swap16(size);
    uint16_t swappedType = swap16(recordType);
    out.write(reinterpret_cast<const char*>(&swappedSize), 2);
    out.write(reinterpret_cast<const char*>(&swappedType), 2);
}

void GdsExporter::writeI2(std::ofstream& out, uint16_t recordType, int16_t val) {
    writeRecordHeader(out, 6, recordType);
    uint16_t swappedVal = swap16((uint16_t)val);
    out.write(reinterpret_cast<const char*>(&swappedVal), 2);
}

void GdsExporter::writeI4(std::ofstream& out, uint16_t recordType, int32_t val) {
    writeRecordHeader(out, 8, recordType);
    uint32_t swappedVal = swap32((uint32_t)val);
    out.write(reinterpret_cast<const char*>(&swappedVal), 4);
}

void GdsExporter::writeReal8(std::ofstream& out, uint16_t recordType, double val) {
    writeRecordHeader(out, 12, recordType);
    uint64_t ibmFloat = real8ToGDS(val);
    out.write(reinterpret_cast<const char*>(&ibmFloat), 8);
}

void GdsExporter::writeString(std::ofstream& out, uint16_t recordType, const std::string& str) {
    std::string s = str;
    if (s.length() % 2 != 0) s += '\0'; // GDSII strings must be even length
    writeRecordHeader(out, (uint16_t)(4 + s.length()), recordType);
    out.write(s.c_str(), s.length());
}

void GdsExporter::writeNoData(std::ofstream& out, uint16_t recordType) {
    writeRecordHeader(out, 4, recordType);
}

// ========================================================================
//  GEOMETRY WRITERS
// ========================================================================

void GdsExporter::writePolygon(std::ofstream& out, int layer, const std::vector<std::pair<int32_t, int32_t>>& points, int datatype) {
    writeNoData(out, BOUNDARY);
    writeI2(out, LAYER, (int16_t)layer);
    writeI2(out, DATATYPE, (int16_t)datatype);
    
    // XY record: header(4) + (num_points * 8 bytes for two I4s)
    uint16_t xySize = (uint16_t)(4 + (points.size() * 8));
    writeRecordHeader(out, xySize, XY);
    
    for (const auto& pt : points) {
        uint32_t x = swap32((uint32_t)pt.first);
        uint32_t y = swap32((uint32_t)pt.second);
        out.write(reinterpret_cast<const char*>(&x), 4);
        out.write(reinterpret_cast<const char*>(&y), 4);
    }
    
    writeNoData(out, ENDEL);
}

void GdsExporter::writeWireRect(std::ofstream& out, int layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int half_width) {
    int32_t minX = std::min(x1, x2) - half_width;
    int32_t maxX = std::max(x1, x2) + half_width;
    int32_t minY = std::min(y1, y2) - half_width;
    int32_t maxY = std::max(y1, y2) + half_width;

    std::vector<std::pair<int32_t, int32_t>> boundary = {
        {minX, minY},
        {maxX, minY},
        {maxX, maxY},
        {minX, maxY},
        {minX, minY}
    };

    writePolygon(out, layer, boundary, 20); // sky130: metal routing shapes use datatype 20
}

void GdsExporter::writeSRef(std::ofstream& out, const std::string& structName, int32_t x, int32_t y) {
    writeNoData(out, SREF);
    writeString(out, SNAME, structName);
    
    // XY record: header(4) + 1 point (8 bytes)
    writeRecordHeader(out, 12, XY);
    uint32_t sx = swap32((uint32_t)x);
    uint32_t sy = swap32((uint32_t)y);
    out.write(reinterpret_cast<const char*>(&sx), 4);
    out.write(reinterpret_cast<const char*>(&sy), 4);
    
    writeNoData(out, ENDEL);
}

// ========================================================================
//  LAYER TRANSLATION
// ========================================================================

int GdsExporter::mapLayerToGds(int routerLayer) {
    // sky130 layer stack: li1(67) met1(68) met2(69) met3(70) met4(71) met5(72)
    switch (routerLayer) {
        case 1:  return GDS_LI1;
        case 2:  return GDS_M1;
        case 3:  return GDS_M2;
        case 4:  return GDS_M3;
        case 5:  return GDS_M4;
        case 6:  return GDS_M5;
        default: return GDS_LI1;
    }
}

int GdsExporter::mapViaToGds(int fromLayer, int toLayer) {
    int lo = std::min(fromLayer, toLayer);
    int hi = std::max(fromLayer, toLayer);
    if (lo == 1 && hi == 2) return GDS_VIA12; // mcon  (li1→met1)
    if (lo == 2 && hi == 3) return GDS_VIA23; // via   (met1→met2)
    if (lo == 3 && hi == 4) return GDS_VIA34; // via2  (met2→met3)
    if (lo == 4 && hi == 5) return GDS_VIA45; // via3  (met3→met4)
    if (lo == 5 && hi == 6) return GDS_VIA56; // via4  (met4→met5)
    return GDS_VIA12;
}

// ========================================================================
//  MAIN EXPORT FUNCTION — Hierarchical GDSII Generation
// ========================================================================

bool GdsExporter::exportGds(const std::string& filename, Design* design) {
    std::ofstream out(filename, std::ios::binary);
    if (!out) {
        std::cerr << "Error: Could not open " << filename << " for writing GDSII." << std::endl;
        return false;
    }
    
    std::cout << "=== EXPORTING GDSII ===" << std::endl;
    std::cout << "  Generating byte-swapped big-endian binary..." << std::endl;

    // Scaling factor: our design coords are in grid units, 
    // we scale them to nanometers for the GDSII database units
    const int SCALE = 100; // 1 grid unit = 100 nm = 0.1 um
    const int HALF_WIDTH = 10 * SCALE; // Wire half-width in DB units
    const int VIA_SIZE = 8 * SCALE;    // Via square half-width
    const int PIN_SIZE = 5 * SCALE;    // Pin rectangle half-width

    // ============================
    // 1. GDSII LIBRARY HEADER
    // ============================
    writeI2(out, HEADER, 600); // GDSII version 6.0
    
    // BGNLIB (28 bytes of date/time padding)
    writeRecordHeader(out, 28, BGNLIB);
    for (int i = 0; i < 12; ++i) { uint16_t pad = 0; out.write(reinterpret_cast<const char*>(&pad), 2); }
    
    writeString(out, LIBNAME, "OPENEDA_LIB");
    
    // Units: 1 database unit = 1 nm = 0.001 um = 1e-9 m
    writeRecordHeader(out, 20, UNITS);
    uint64_t userUnits = real8ToGDS(0.001);
    uint64_t dbUnits = real8ToGDS(1e-9);
    out.write(reinterpret_cast<const char*>(&userUnits), 8);
    out.write(reinterpret_cast<const char*>(&dbUnits), 8);
    
    // ============================
    // 2. CELL LIBRARY STRUCTURES
    // ============================
    // Each unique CellDef gets its own GDSII STRUCTURE with:
    //   - Bounding box polygon on GDS_CELL layer
    //   - Pin rectangles on GDS_PIN layer
    
    int cellStructCount = 0;
    std::set<std::string> writtenCells;
    
    if (design->cellLibrary) {
        for (auto& [cellName, cellDef] : design->cellLibrary->cells) {
            if (!cellDef || writtenCells.count(cellName)) continue;
            writtenCells.insert(cellName);
            
            // BGNSTR
            writeRecordHeader(out, 28, BGNSTR);
            for (int i = 0; i < 12; ++i) { uint16_t pad = 0; out.write(reinterpret_cast<const char*>(&pad), 2); }
            writeString(out, STRNAME, cellName);
            
            // Cell bounding box (origin-relative, lower-left = 0,0)
            int32_t cw = (int32_t)(cellDef->width * 1000 * SCALE);
            int32_t ch = (int32_t)(cellDef->height * 1000 * SCALE);
            
            std::vector<std::pair<int32_t, int32_t>> bbox = {
                {0, 0}, {cw, 0}, {cw, ch}, {0, ch}, {0, 0}
            };
            writePolygon(out, GDS_CELL, bbox);
            
            // Pin rectangles
            for (const auto& pin : cellDef->pins) {
                int32_t px = (int32_t)(pin.dx * 1000 * SCALE) + cw / 2; // offset from cell center
                int32_t py = (int32_t)(pin.dy * 1000 * SCALE) + ch / 2;
                
                std::vector<std::pair<int32_t, int32_t>> pinRect = {
                    {px - PIN_SIZE, py - PIN_SIZE},
                    {px + PIN_SIZE, py - PIN_SIZE},
                    {px + PIN_SIZE, py + PIN_SIZE},
                    {px - PIN_SIZE, py + PIN_SIZE},
                    {px - PIN_SIZE, py - PIN_SIZE}
                };
                writePolygon(out, GDS_PIN, pinRect);
            }
            
            writeNoData(out, ENDSTR);
            cellStructCount++;
        }
    }
    
    std::cout << "  Wrote " << cellStructCount << " cell structure definitions." << std::endl;
    
    // ============================
    // 3. TOP-LEVEL STRUCTURE
    // ============================
    writeRecordHeader(out, 28, BGNSTR);
    for (int i = 0; i < 12; ++i) { uint16_t pad = 0; out.write(reinterpret_cast<const char*>(&pad), 2); }
    writeString(out, STRNAME, "TOP");
    
    // --- 3A. SREF INSTANCES (Place each gate at its legalized coordinates) ---
    int srefCount = 0;
    for (auto* inst : design->instances) {
        if (!inst || !inst->type) continue;
        
        int32_t ix = (int32_t)(inst->x * SCALE);
        int32_t iy = (int32_t)(inst->y * SCALE);
        
        writeSRef(out, inst->type->name, ix, iy);
        srefCount++;
    }
    
    std::cout << "  Placed " << srefCount << " cell instances via SREF." << std::endl;
    
    // --- 3B. SIGNAL ROUTING (1D Centerlines → 2D Copper Rectangles) ---
    int wireCount = 0;
    int viaCount = 0;
    int pdnSegCount = 0;
    
    for (auto* net : design->nets) {
        if (net->routePath.size() < 2) continue;
        
        bool isPDN = (net->name == "VDD" || net->name == "VSS");
        
        for (size_t i = 0; i < net->routePath.size() - 1; i += 1) {
            auto& p1 = net->routePath[i];
            auto& p2 = net->routePath[i + 1];
            
            int32_t x1 = p1.x * SCALE;
            int32_t y1 = p1.y * SCALE;
            int32_t x2 = p2.x * SCALE;
            int32_t y2 = p2.y * SCALE;
            
            // Layer change = Via (square polygon)
            if (p1.layer != p2.layer) {
                int viaLayer = mapViaToGds(p1.layer, p2.layer);
                
                std::vector<std::pair<int32_t, int32_t>> viaPoly = {
                    {x2 - VIA_SIZE, y2 - VIA_SIZE},
                    {x2 + VIA_SIZE, y2 - VIA_SIZE},
                    {x2 + VIA_SIZE, y2 + VIA_SIZE},
                    {x2 - VIA_SIZE, y2 + VIA_SIZE},
                    {x2 - VIA_SIZE, y2 - VIA_SIZE}
                };
                writePolygon(out, viaLayer, viaPoly, 44); // sky130: via cuts use datatype 44
                viaCount++;
                continue;
            }

            // Same layer = Wire rectangle (sky130: metal shapes use datatype 20)
            int gdsLayer = mapLayerToGds(p1.layer);
            writeWireRect(out, gdsLayer, x1, y1, x2, y2, HALF_WIDTH);
            
            if (isPDN) pdnSegCount++;
            else wireCount++;
        }
    }
    
    std::cout << "  Exported " << wireCount << " signal wire segments." << std::endl;
    std::cout << "  Exported " << viaCount << " via cuts." << std::endl;
    std::cout << "  Exported " << pdnSegCount << " PDN power grid segments." << std::endl;
    
    // --- 3C. CLOSE TOP STRUCTURE ---
    writeNoData(out, ENDSTR);
    
    // ============================
    // 4. CLOSE LIBRARY
    // ============================
    writeNoData(out, ENDLIB);
    
    out.close();
    
    // Calculate file size
    std::ifstream checkFile(filename, std::ios::binary | std::ios::ate);
    long fileSize = checkFile.tellg();
    checkFile.close();
    
    std::cout << "  Successfully wrote " << filename << " (" << fileSize << " bytes)" << std::endl;
    std::cout << "  Summary: " << cellStructCount << " cells, " 
              << srefCount << " instances, "
              << wireCount << " wires, " 
              << viaCount << " vias, " 
              << pdnSegCount << " PDN segments" << std::endl;
    
    return true;
}
