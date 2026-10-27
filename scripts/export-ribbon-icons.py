#!/usr/bin/env python3
"""Extract the original Paint vector art for the offline SVG/PNG exporter.
Run this, build target paint-export-ribbon-icons, then run that executable.
The resulting PNG bytes are embedded; the app requires no asset paths.
"""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
source = (root / 'src/ribbon.cpp').read_text()
art = source[source.index('void classic_icon('):source.index('bool ribbon_button(')]
for a,b in [('ImDrawList','SvgIcon'),('ImVec2','IconPoint'),('ImU32','IconColor'),('IM_COL32','icon_rgba')]:
    art = art.replace(a,b)
# Optical-size shape artwork: intact closed contours and visible callout tails.
start = art.index('    if (icon >= 100')
end = art.index('    switch (icon)', start)
art = art[:start] + r'''
    if (icon >= 100 && icon < 100 + shape_count) {
        Shape shape=static_cast<Shape>(icon-100);
        const char* path=nullptr;
        if(shape==Shape::Rectangle) path="M2.5 3.5 H13.5 V12.5 H2.5 Z";
        if(shape==Shape::RoundedRectangle) path="M4.5 3 H11.5 Q14 3 14 5.5 V10.5 Q14 13 11.5 13 H4.5 Q2 13 2 10.5 V5.5 Q2 3 4.5 3 Z";
        if(shape==Shape::Polygon) path="M2 12 L4 3 L8 6 L13 2 L14 12 Z";
        if(shape==Shape::RoundedCallout) path="M4 2.5 H12 Q14 2.5 14 4.5 V9 Q14 11 12 11 H8 L4 14 L5 11 H4 Q2 11 2 9 V4.5 Q2 2.5 4 2.5 Z";
        if(shape==Shape::OvalCallout) path="M5 11 C0 10 0 3 6 2.5 C15 0 18 10 9 11 L4 14 Z";
        if(shape==Shape::Bezier) path="M2 13 C5 -1 11 17 14 3";
        if(shape==Shape::Arc) path="M2 12 C2 3 14 3 14 12";
        if(path) {draw.ShapePath(path,color,1.25f,size/16);return;}
        float bottom=shape==Shape::CloudCallout ? size*.72f : size-2;
        std::vector<Point> points=shape_points(shape,{2,shape==Shape::Oval?size*.25f:2},{size-2,shape==Shape::Oval?size*.75f:bottom});
        std::ostringstream contour;
        for(std::size_t index=0;index<points.size();++index) contour<<(index==0?"M":" L")<<points[index].x<<" "<<points[index].y;
        if(points.size()>2) contour<<" Z";
        draw.ShapePath(contour.str(),color,size<=16?1.25f:1.5f);
        return;
    }
''' + art[end:]
art = art.replace('    switch (icon) {', """    if (icon == 9) {
        draw.ShapePath("M3 14 L8 2 L13 14 M5 9 H11 M1.5 14 H5 M11 14 H14.5",color,1.6f,s/16);
        return;
    }
    if (icon == 15 || icon == 16) {
        const char* path=icon==15 ? "M5 4 C12 1 17 10 10 14 M5 4 L5 1 M5 4 L8 5"
            : "M11 4 C4 1 -1 10 6 14 M11 4 L11 1 M11 4 L8 5";
        draw.ShapePath(path,icon_rgba(61,132,185,255),1.7f,s/16);
        return;
    }
    if (icon == 23) {
        for (int i=0;i<4;++i) draw.AddRectFilled({s*.08f,s*(.10f+i*.22f)},{s*.92f,s*(.13f+i*.25f)},color);
        return;
    }
    if (icon == 24) {
        const IconColor colors[] = {icon_rgba(221,60,60,255),icon_rgba(235,165,44,255),icon_rgba(84,170,99,255),icon_rgba(42,145,202,255),icon_rgba(144,84,166,255)};
        for(int row=0;row<4;++row) for(int col=0;col<5;++col) {
            IconColor color=colors[col];
            draw.AddRectFilled({s*(.04f+col*.18f),s*(.1f+row*.21f)},{s*(.20f+col*.18f),s*(.28f+row*.21f)},color);
        }
        return;
    }
    switch (icon) {""")
output = '#include "ribbon_svg_writer.hpp"\nusing namespace paint;\n' + art
output += r'''
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::filesystem::path root(argv[1]);
    std::filesystem::create_directories(root / "assets/ribbon");
    std::ofstream header(root / "src/forms/ribbon_icons.hpp");
    header << "// Generated from the original vector art by scripts/export-ribbon-icons.py.\n#pragma once\n#include <cstddef>\n#include <span>\nnamespace paint::forms {\n";
    for(int logical : {16,24,32}) {
        std::filesystem::path directory=root/"assets/ribbon"/std::to_string(logical);
        std::filesystem::create_directories(directory);
        for (int index=0; index<25+shape_count; ++index) {
            int icon=index<25?index:100+index-25;
            SvgIcon drawing(logical);
            classic_icon(drawing,icon,{0,0},static_cast<float>(logical),icon_rgba(34,62,89,255));
            std::filesystem::path svg=directory/(std::to_string(icon)+".svg");
            std::ofstream(svg)<<drawing.svg();
            Image image=rasterize_svg(svg.string(),logical*2,logical*2);
            std::vector<std::uint8_t> png=encode_png(image);
            header<<"inline constexpr unsigned char icon_"<<icon<<"_"<<logical<<"[] = {";
            for(std::size_t i=0;i<png.size();++i) header<<(i%24==0?"\n":"")<<static_cast<unsigned>(png[i])<<",";
            header<<"\n};\n";
        }
    }
    header<<"inline std::span<const std::byte> ribbon_icon_png(int icon,int size=32) {\n";
    for(int logical : {16,24,32}) {
        header<<"if(size=="<<logical<<") { switch(icon) {\n";
        for(int index=0;index<25+shape_count;++index) {
            int icon=index<25?index:100+index-25;
            header<<"case "<<icon<<": return std::as_bytes(std::span(icon_"<<icon<<"_"<<logical<<"));\n";
        }
        header<<"default: return {};\n} }\n";
    }
    header<<"return {};\n}\n}\n";
}
'''
output = '#include <filesystem>\n' + output
(root / 'astra').mkdir(exist_ok=True)
(root / 'astra/ribbon-export.cpp').write_text(output)
