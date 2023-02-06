#include "Font.h"
#define STB_RECT_PACK_IMPLEMENTATION
#include <stb_rect_pack.h> // optional, used for better bitmap packing
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <ErrorHandling.h>
#include <fstream>
#include "libavstream/geometry/mesh_interface.hpp"
#include <GeometryStore.h>

using namespace teleport;


bool teleport::Font::ExtractFont(FontAtlas &fontAtlas,const char *ttf_path_utf8,const char *generate_texture_path_utf8,const char *atlas_chars,avs::Texture &avsTexture
	,std::vector<int> sizes)
{
	fontAtlas.font_texture_path=generate_texture_path_utf8;
	using namespace std;
	size_t numSizes=sizes.size();
    /* load font file */
    unsigned char* fontBuffer=nullptr;
    
	{
		long fsize=0;
		ifstream loadFile(ttf_path_utf8, std::ios::binary);
		if(!loadFile.good())
			return 0;
		loadFile.seekg(0,ios::end);
        fsize = (long)loadFile.tellg();
		loadFile.seekg(0,ios::beg);
		fontBuffer = new unsigned char[fsize];
		loadFile.read((char*)fontBuffer,fsize);
	}
	#define NUM_GLYPHS 95
	
    // setup glyph info stuff, check stb_truetype.h for definition of structs
	vector<vector<stbtt_packedchar>> glyph_metrics(numSizes);
	const int first_char=32;
    vector<stbtt_pack_range> ranges(numSizes);
	for(size_t i=0;i<numSizes;i++)
	{
		auto &fontMap=fontAtlas.fontMaps[sizes[i]];
		fontMap.glyphs.resize(NUM_GLYPHS);
		glyph_metrics[i].resize(NUM_GLYPHS);
		stbtt_pack_range &range					=ranges[i];
		range.font_size							=float(sizes[i]);
		range.first_unicode_codepoint_in_range	=first_char;
		range.array_of_unicode_codepoints		=NULL;
		range.num_chars							=NUM_GLYPHS;
		range.chardata_for_range				=glyph_metrics[i].data();
		range.h_oversample						=0;
		range.v_oversample						=0;
	}
    // make a most likely large enough bitmap, adjust to font type, number of sizes and glyphs and oversampling
    int width = 1024;
    int max_height = 1024;
    unsigned char* bitmap = new unsigned char[max_height*width];
    // do the packing, based on the ranges specified
    stbtt_pack_context pc;
    stbtt_PackBegin(&pc, bitmap, width, max_height, 0, 1, NULL);   
    stbtt_PackSetOversampling(&pc, 1, 1); // say, choose 3x1 oversampling for subpixel positioning
    stbtt_PackFontRanges(&pc, fontBuffer, 0, ranges.data(), (int)ranges.size());
    stbtt_PackEnd(&pc);

    // get the global metrics for each size/range
    stbtt_fontinfo info;
    stbtt_InitFont(&info, fontBuffer, stbtt_GetFontOffsetForIndex(fontBuffer,0));

    vector<float> ascents(numSizes);
    vector<float> descents(numSizes);
    vector<float> linegaps(numSizes);

    for (int i = 0; i < numSizes; i++)
	{
    	float size = ranges[i].font_size;
        float scale = stbtt_ScaleForPixelHeight(&info, ranges[i].font_size);
        int a, d, l;
        stbtt_GetFontVMetrics(&info, &a, &d, &l);
        
        ascents[i]  = a*scale;
        descents[i] = d*scale;
        linegaps[i] = l*scale;
		
		auto &fontMap=fontAtlas.fontMaps[sizes[i]];
		fontMap.lineHeight=ascents[i]- descents[i] + linegaps[i];
    }

    // calculate fill rate and crop the bitmap
    int filled = 0;
    int height = 0;
    for (int i = 0; i < numSizes; i++)
	{
		auto &fontMap=fontAtlas.fontMaps[sizes[i]];
		
        for (int j = 0; j < NUM_GLYPHS; j++)
		{
			stbtt_packedchar &metric=glyph_metrics[i][j];
            if (metric.y1 > height)
				height = metric.y1;
            filled += (metric.x1 - metric.x0)*(metric.y1 - metric.y0);
			
			fontMap.glyphs[j].x0		=metric.x0;
			fontMap.glyphs[j].y0		=metric.y0;
			fontMap.glyphs[j].x1		=metric.x1;
			fontMap.glyphs[j].y1		=metric.y1;
			fontMap.glyphs[j].xOffset	=metric.xoff;
			fontMap.glyphs[j].yOffset	=metric.yoff;
			fontMap.glyphs[j].xAdvance	=metric.xadvance;
			fontMap.glyphs[j].xOffset2	=metric.xoff2;
			fontMap.glyphs[j].yOffset2	=metric.yoff2;
        }
    }
	avsTexture.name=ttf_path_utf8;
	avsTexture.width=width;
	avsTexture.height=height;
	avsTexture.depth=1;
	avsTexture.bytesPerPixel=4;
	avsTexture.arrayCount=1;
	avsTexture.mipCount=1;
	avsTexture.format=avs::TextureFormat::G8;
	avsTexture.compression=avs::TextureCompression::PNG;
	avsTexture.compressed=true;
    stbi_write_png(generate_texture_path_utf8, width, height, 1, bitmap, 0);

	int len=0;
	unsigned char *png = stbi_write_png_to_mem(bitmap, 0, width, height, 1, &len);
	if (!png)
		return false;


	//std::ifstream ifs_png(generate_texture_path_utf8, std::ios::in | std::ios::binary);
	//std::vector<uint8_t> contents((std::istreambuf_iterator<char>(ifs_png)), std::istreambuf_iterator<char>());


	uint32_t imageSize=len;//height*pc.stride_in_bytes;
	uint16_t numImages=1;
	uint32_t offset0=uint32_t(sizeof(numImages)+sizeof(imageSize));
	avsTexture.dataSize=imageSize+offset0;
	
	unsigned char *target=new unsigned char[avsTexture.dataSize];
	avsTexture.data=target;
	memcpy(target,&numImages,sizeof(numImages));
	target+=sizeof(numImages);
	memcpy(target,&offset0,sizeof(offset0));
	target+=sizeof(imageSize);
	memcpy(target,png,imageSize);
	STBIW_FREE(png);

    // create file
    TELEPORT_INTERNAL_COUT("height = {0}, fill rate = {1}\n", height, 100*filled/(double)(width*height)); fflush(stdout);
	//std::string png_filename=std::string(ttf_path_utf8)+".png";


	//void write_data(void *context, void *data, int size);
	//int res=stbi_write_png_to_func(stbi_write_func *func, this, int x, int y, int comp, const void *data, int stride_bytes)

    // print info
    if (0)
	{
        for (int j = 0; j < numSizes; j++)
		{
            TELEPORT_INTERNAL_COUT("size    {0}\n", ranges[j].font_size);
            TELEPORT_INTERNAL_COUT("ascent  {0}\n", ascents[j]);
            TELEPORT_INTERNAL_COUT("descent {0}\n", descents[j]);
            TELEPORT_INTERNAL_COUT("linegap {0}\n", linegaps[j]);
            vector<stbtt_packedchar> m = glyph_metrics[j];
			 for (int i = 0; i < NUM_GLYPHS; i++)
			{
                TELEPORT_INTERNAL_COUT("    '{0}':  (x0,y0) = ({1},{2}),  (x1,y1) = ({3},{4}),  (xoff,yoff) = ({5},{6}),  (xoff2,yoff2) = ({7},{8}),  xadvance = {9}\n", 
                       32+i, m[i].x0, m[i].y0, m[i].x1, m[i].y1, m[i].xoff, m[i].yoff, m[i].xoff2, m[i].yoff2, m[i].xadvance);
            }
        }   
    }

    delete[] fontBuffer;
    delete[] bitmap;
    
    return 0;
}
	
void teleport::Font::Free(avs::Texture &avsTexture)
{
	delete [] avsTexture.data;
	avsTexture.data=0;
}

teleport::Font &teleport::Font::GetInstance()
{
	static Font font;
	return font;
}

teleport::Font::~Font()
{
	for(auto a:interopFontAtlases)
	{
		for(int i=0;i<a.second.numMaps;i++)
		{
			InteropFontMap &fontMap=a.second.fontMaps[i];
			delete [] fontMap.fontGlyphs;
		}
		delete [] a.second.fontMaps;
	}
	interopFontAtlases.clear();
}

bool teleport::Font::GetInteropFontAtlas(std::string path,InteropFontAtlas *interopFontAtlas)
{
	avs::uid uid=teleport::GeometryStore::GetInstance().PathToUid(path);
	if(!uid)
		return false;
	InteropFontAtlas *sourceAtlas=nullptr;
	auto &f=interopFontAtlases.find(path);
	if(f==interopFontAtlases.end())
	{
		sourceAtlas=&(interopFontAtlases[path]);
		const FontAtlas *fontAtlas=teleport::GeometryStore::GetInstance().getFontAtlas(uid);
		sourceAtlas->numMaps=(int)fontAtlas->fontMaps.size();
		sourceAtlas->fontMaps=new InteropFontMap[sourceAtlas->numMaps];
		int i=0;
		for(auto m:fontAtlas->fontMaps)
		{
			sourceAtlas->fontMaps[i].size=m.first;
			sourceAtlas->fontMaps[i].numGlyphs=(int)m.second.glyphs.size();
			sourceAtlas->fontMaps[i].fontGlyphs=new Glyph[m.second.glyphs.size()];
			memcpy(sourceAtlas->fontMaps[i].fontGlyphs,m.second.glyphs.data(),sizeof(Glyph)*m.second.glyphs.size());
			i++;
		}
	}
	sourceAtlas=&(interopFontAtlases[path]);
	interopFontAtlas->fontMaps=sourceAtlas->fontMaps;
	return true;
}