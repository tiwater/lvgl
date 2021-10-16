#include "lvgl.h"
#include "../misc/lv_fs.h"
#if CONFIG_SPIRAM_USE_CAPS_ALLOC
//#include "esp_head_caps.h"
#endif

typedef struct{
    uint16_t min;
    uint16_t max;
    uint8_t  bpp;
    uint8_t  reserved[3];
}x_header_t;

typedef struct{
    uint32_t pos;
}x_table_t;

typedef struct{
    uint8_t adv_w;
    uint8_t box_w;
    uint8_t box_h;
    int8_t  ofs_x;
    int8_t  ofs_y;
    uint8_t r;
}glyph_dsc_t;


static x_header_t __g_xbf_hd = {
    .min = 0x0009,
    .max = 0xffe5,
    .bpp = 4,
};

#define FONT_PARTITION_TYPE   (0x66)


#define LOAD_FONT_FROM_PARTITION   1
#define LOAD_FONT_FROM_MEMORY   2
#define LOAD_FONT_FROM_FILE   3

#define LOAD_FONT_METHOD LOAD_FONT_FROM_MEMORY


#if LOAD_FONT_METHOD == LOAD_FONT_FROM_PARTITION
extern void port_font_partition_read(uint8_t* buf, int part_type, int offset, int size);
static uint8_t __g_font_buf[96];
#elif LOAD_FONT_METHOD == LOAD_FONT_FROM_MEMORY

#if CONFIG_SPIRAM_USE_CAPS_ALLOC
extern void *heap_caps_malloc( size_t size, uint32_t caps );
#define MALLOC_CAP_SPIRAM           (1<<10)
#define MALLOC_CAP_8BIT             (1<<2)
#endif

uint8_t* fontbuf = NULL;
static const char font_name[] = "S:/system/syst_16.bin";

static void load_font() {
    lv_fs_res_t res = LV_FS_RES_OK;
    lv_fs_file_t file;
    if((res = lv_fs_open(&file, font_name, LV_FS_MODE_RD)) == LV_FS_RES_OK) {
        if((res = lv_fs_seek(&file, 0, LV_FS_SEEK_END)) == LV_FS_RES_OK){
            uint32_t length;
            if((res = lv_fs_tell(&file, &length)) == LV_FS_RES_OK){
#if CONFIG_SPIRAM_USE_CAPS_ALLOC
                fontbuf = heap_caps_malloc(length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
                fontbuf = malloc(length);
#endif
                if(fontbuf == NULL){
                    LV_LOG_ERROR("Out of memory for font loading");
                    lv_fs_close(&file);
                    return;
                }
                lv_fs_seek(&file, 0, LV_FS_SEEK_SET);
                if(lv_fs_read(&file, fontbuf, length, NULL) != LV_FS_RES_OK) {
                    free(fontbuf);
                    fontbuf = NULL;
                    LV_LOG_ERROR("Error reading font file: %s", font_name);
                }
            } else {
                LV_LOG_ERROR("Error %d on check file length", res);
            }
        } else {
            LV_LOG_ERROR("Error %d on find file end", res);
        }
        lv_fs_close(&file);
    } else {
        LV_LOG_ERROR("Error %d on opening font file: %s", res, font_name);
    }
}
#else

#define CACHE_SIZE (50)
typedef struct {
    uint32_t offset;
    int size;
    uint8_t buf[96];
} font_cache_t;
static font_cache_t font_cache[CACHE_SIZE];
static int wptr = 0;
static int cache_count = 0;

static const char font_name[] = "S:/system/syst_16.bin";
static lv_fs_file_t* file = NULL;

static lv_fs_res_t open_font_file() {
    //Optimize the speed, only open the font file on the first read, then never close it
    lv_fs_res_t res = LV_FS_RES_OK;
    if(file == NULL){
        file = malloc(sizeof(lv_fs_file_t));
        if(file == NULL) {
            res = LV_FS_RES_OUT_OF_MEM;
            return res;
        }
        memset(font_cache, 0, sizeof(font_cache_t) * CACHE_SIZE);
        res = lv_fs_open(file, font_name, LV_FS_MODE_RD);
    }
    return res;
}

static void close_font_file() {
    if(file){
        lv_fs_close(file);
        free(file);
        file = NULL;
    }
}
#endif

static uint8_t *__user_font_getdata(uint32_t offset, int size){

#if LOAD_FONT_METHOD == LOAD_FONT_FROM_PARTITION
    port_font_partition_read(__g_font_buf, FONT_PARTITION_TYPE, offset, size);
    return __g_font_buf;
#elif LOAD_FONT_METHOD == LOAD_FONT_FROM_MEMORY
    if(fontbuf == NULL){
        load_font();
    }
    return (uint8_t*)(fontbuf + offset);
#else
    //file_rw_offset(mp_obj_t f, const char* buf, mp_uint_t offset, mp_uint_t buf_len, byte flags);
    uint8_t * buf = NULL;
    int i = 0;
    for(;i<cache_count;i++){
        if(font_cache[i].offset == offset && font_cache[i].size == size){
            //Found
            return font_cache[i].buf;
        }
    }
    lv_fs_res_t res = open_font_file();

    if(res != LV_FS_RES_OK){
        // memset(__g_font_buf, 0, size);
        LV_LOG_WARN("Error loading font file: %s\n", font_name);
        return buf;
    }
    
    lv_fs_seek(file, offset, LV_FS_SEEK_SET);

    if(lv_fs_read(file, font_cache[wptr].buf, size, NULL) != LV_FS_RES_OK) {
        memset(font_cache[wptr].buf, 0, size);
        LV_LOG_WARN("Error reading font file: %s\n", font_name);
    } else {
        font_cache[wptr].offset = offset;
        font_cache[wptr].size = size;
        buf = font_cache[wptr].buf;
        wptr++;
        if(wptr >= CACHE_SIZE){
            wptr = 0;
        }
        if(cache_count < CACHE_SIZE){
            cache_count++;
        }
    }
    // if(lv_fs_read(file, __g_font_buf, size, NULL) != LV_FS_RES_OK) {
    //     memset(__g_font_buf, 0, size);
    //     LV_LOG_WARN("Error reading font file: %s\n", font_name);
    // }

//    close_font_file();

    return buf;
#endif
}


static const uint8_t * __user_font_get_bitmap(const lv_font_t * font, uint32_t unicode_letter) {
    if( unicode_letter>__g_xbf_hd.max || unicode_letter<__g_xbf_hd.min ) {
        return NULL;
    }
    uint32_t unicode_offset = sizeof(x_header_t)+(unicode_letter-__g_xbf_hd.min)*4;
    uint32_t *p_pos = (uint32_t *)__user_font_getdata(unicode_offset, 4);
    if( p_pos[0] != 0 ) {
        uint32_t pos = p_pos[0];
        glyph_dsc_t * gdsc = (glyph_dsc_t*)__user_font_getdata(pos, sizeof(glyph_dsc_t));
        return __user_font_getdata(pos+sizeof(glyph_dsc_t), gdsc->box_w*gdsc->box_h*__g_xbf_hd.bpp/8);
    }
    return NULL;
}


static bool __user_font_get_glyph_dsc(const lv_font_t * font, lv_font_glyph_dsc_t * dsc_out, uint32_t unicode_letter, uint32_t unicode_letter_next) {
    if( unicode_letter>__g_xbf_hd.max || unicode_letter<__g_xbf_hd.min ) {
        return NULL;
    }
    uint32_t unicode_offset = sizeof(x_header_t)+(unicode_letter-__g_xbf_hd.min)*4;
    uint32_t *p_pos = (uint32_t *)__user_font_getdata(unicode_offset, 4);
    if( p_pos[0] != 0 ) {
        glyph_dsc_t * gdsc = (glyph_dsc_t*)__user_font_getdata(p_pos[0], sizeof(glyph_dsc_t));
        dsc_out->adv_w = gdsc->adv_w;
        dsc_out->box_h = gdsc->box_h;
        dsc_out->box_w = gdsc->box_w;
        dsc_out->ofs_x = gdsc->ofs_x;
        dsc_out->ofs_y = gdsc->ofs_y;
        dsc_out->bpp   = __g_xbf_hd.bpp;
        return true;
    }
    return false;
}

const lv_font_t lv_font_syst_16 = {
    .get_glyph_bitmap = __user_font_get_bitmap,
    .get_glyph_dsc = __user_font_get_glyph_dsc,
    .line_height = 16,
    .base_line = 0,
};

