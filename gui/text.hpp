#pragma once
#include "font_plex_mono.hpp"
#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "../src/third_party/stb_truetype.h"

struct LcGlyph {
    float x0=0,y0=0,x1=0,y1=0,u0=0,v0=0,u1=0,v1=0,advance=0;
};
class LcTextAtlas {
public:
    static const int size=1024;
    LcTextAtlas():font_(lc_plex_mono_ttf,lc_plex_mono_ttf+lc_plex_mono_ttf_size),pixels_(size*size*4,0){
        if(!stbtt_InitFont(&info_,font_.data(),0))valid_=false;
        for(std::size_t i=0;i<pixels_.size();i+=4)pixels_[i]=pixels_[i+1]=pixels_[i+2]=255;
        int ascent=0,descent=0,line=0;stbtt_GetFontVMetrics(&info_,&ascent,&descent,&line);ascent_=ascent;
    }
    bool valid()const{return valid_;}
    const std::vector<unsigned char>& pixels()const{return pixels_;}
    bool dirty()const{return dirty_;}
    void clearDirty(){dirty_=false;}
    float ascent(float height)const{return ascent_*stbtt_ScaleForPixelHeight(&info_,height);}
    LcGlyph glyph(std::uint32_t cp,float height){
        int pixels=std::max(6,static_cast<int>(height+0.5f));std::uint64_t key=(static_cast<std::uint64_t>(pixels)<<32)|cp;auto found=cache_.find(key);if(found!=cache_.end())return found->second;
        float scale=stbtt_ScaleForPixelHeight(&info_,static_cast<float>(pixels));int advance=0,left=0,x0=0,y0=0,x1=0,y1=0;stbtt_GetCodepointHMetrics(&info_,cp,&advance,&left);stbtt_GetCodepointBitmapBox(&info_,cp,scale,scale,&x0,&y0,&x1,&y1);
        LcGlyph g;g.advance=advance*scale;g.x0=static_cast<float>(x0);g.y0=static_cast<float>(y0);g.x1=static_cast<float>(x1);g.y1=static_cast<float>(y1);int w=std::max(0,x1-x0),h=std::max(0,y1-y0);
        if(w&&h){if(shelfX_+w+1>=size){shelfX_=1;shelfY_+=shelfHeight_+1;shelfHeight_=0;}if(shelfY_+h+1>=size){g.advance=0;cache_[key]=g;return g;}std::vector<unsigned char> bitmap(static_cast<std::size_t>(w)*h);stbtt_MakeCodepointBitmap(&info_,bitmap.data(),w,h,w,scale,scale,cp);for(int y=0;y<h;++y)for(int x=0;x<w;++x){std::size_t at=((shelfY_+y)*size+shelfX_+x)*4;pixels_[at+3]=bitmap[y*w+x];}g.u0=static_cast<float>(shelfX_)/size;g.v0=static_cast<float>(shelfY_)/size;g.u1=static_cast<float>(shelfX_+w)/size;g.v1=static_cast<float>(shelfY_+h)/size;shelfX_+=w+1;shelfHeight_=std::max(shelfHeight_,h);dirty_=true;}
        cache_[key]=g;return g;
    }
private:
    std::vector<unsigned char> font_;
    std::vector<unsigned char> pixels_;
    std::unordered_map<std::uint64_t,LcGlyph> cache_;
    stbtt_fontinfo info_{};int ascent_=0,shelfX_=1,shelfY_=1,shelfHeight_=0;bool valid_=true,dirty_=false;
};
