#include "audio_player.h"
#include "dac.h"
#include "tim.h"
#include "fatfs.h"

#define DMA_BUF_SIZE  1024   // 每个缓冲区的半字个数

extern SD_HandleTypeDef hsd;
extern void MX_SDIO_SD_Init(void);

static FATFS fs;
static FIL file;
static FRESULT mount_res; // 保存 f_mount 返回值
static FRESULT open_res;  // 保存 f_open 返回值

/* 整个 DMA 缓冲区，长度为 2*DMA_BUF_SIZE */
static uint16_t dma_buf[DMA_BUF_SIZE * 2];
/* 填充请求标志 */
static volatile uint8_t fill_front = 0;  // 需要填充前半
static volatile uint8_t fill_back  = 0;  // 需要填充后半
static volatile uint8_t playback_started = 0;  // 已开始播放


/* 临时存储从 SD 卡读取的原始 8 位数据 */
static uint8_t read_temp[512];

/* 填充指定的半区 */
static void FillBuffer(uint16_t *buf)
{
    uint32_t samples_left = DMA_BUF_SIZE;
    uint32_t buf_pos = 0;

    while (samples_left > 0)
    {
        UINT br;
        uint32_t to_read = (samples_left < 512) ? samples_left : 512;
        f_read(&file, read_temp, to_read, &br);

        if (br == 0)
        {
            f_lseek(&file, 0);
            continue;
        }

        for (UINT i = 0; i < br; i++)
        {
            int16_t ac = (int16_t)read_temp[i] - 128;
            int16_t gain = 1;          //  音量增益，这里调
            ac = ac * gain;

            if (ac > 63)  ac = 63;
            if (ac < -64) ac = -64;

            uint8_t amp_sample = (uint8_t)(ac + 128);
            buf[buf_pos + i] = (uint16_t)((uint32_t)amp_sample * 4095 / 255);
        }

        buf_pos += br;
        samples_left -= br;
			}
}


/* 初始化 */
void AudioPlayer_Init(void)
{
    uint16_t silence = 2048;
    for (int i = 0; i < DMA_BUF_SIZE * 2; i++)
    {
        dma_buf[i] = silence;
    }

    
    HAL_SD_DeInit(&hsd);          // 需要 extern 声明 hsd（见下面）
    HAL_Delay(50);
    MX_SDIO_SD_Init();            // 重新执行硬件初始化
    HAL_Delay(200);               // 额外稳定时间

    mount_res = f_mount(&fs, "", 1);
    if (mount_res != FR_OK)
    {
        Error_Handler();
    }
}




/* 开始播放 */
void AudioPlayer_Start(void)
{
	    open_res = f_open(&file, "MUSIC1.raw", FA_READ);//调试
    if (open_res != FR_OK)
    {
        Error_Handler();
    }//调试

    //预填充整个缓冲区 
    FillBuffer(&dma_buf[0]);
    FillBuffer(&dma_buf[DMA_BUF_SIZE]);

    HAL_TIM_Base_Start(&htim6);
    HAL_DAC_Start(&hdac, DAC_CHANNEL_1);   // 使能 DAC 输出
    HAL_DAC_Start_DMA(&hdac,
                      DAC_CHANNEL_1,
                      (uint32_t*)dma_buf,
                      DMA_BUF_SIZE * 2,
                      DAC_ALIGN_12B_R);

    playback_started = 1;

}



/* 停止播放 */
void AudioPlayer_Stop(void)
{
    HAL_DAC_Stop_DMA(&hdac, DAC_CHANNEL_1);
    HAL_TIM_Base_Stop(&htim6);
    f_close(&file);
    playback_started = 0;

}

/* 主循环中调用 */
void AudioPlayer_Process(void)
{
    if (!playback_started) return;

    if (fill_front)
    {
        fill_front = 0;
        FillBuffer(&dma_buf[0]);   // 填充前半
    }
    if (fill_back)
    {
        fill_back = 0;
        FillBuffer(&dma_buf[DMA_BUF_SIZE]); // 填充后半
    }
}


/* DMA 半传输完成回调 */
void HAL_DAC_ConvHalfCpltCallbackCh1(DAC_HandleTypeDef *hdac)
{
    fill_front = 1;   // 只设置标志，不在中断里填充
}

/* DMA 全传输完成回调 */
void HAL_DAC_ConvCpltCallbackCh1(DAC_HandleTypeDef *hdac)
{
    fill_back = 1;    // 只设置标志，不在中断里填充
}
/* USER CODE BEGIN 1 */
void AudioPlayer_Play(const char *filename)
{
    // 如果正在播放，先停止
    if (playback_started)
    {
        HAL_DAC_Stop_DMA(&hdac, DAC_CHANNEL_1);
        HAL_TIM_Base_Stop(&htim6);
        f_close(&file);
        playback_started = 0;
    }

    // 打开新文件
    if (f_open(&file, filename, FA_READ) != FR_OK)
    {
        Error_Handler();   // 或直接返回，避免死机
    }

    // 预填充缓冲区
    FillBuffer(&dma_buf[0]);
    FillBuffer(&dma_buf[DMA_BUF_SIZE]);

    // 启动播放
    HAL_TIM_Base_Start(&htim6);
    HAL_DAC_Start(&hdac, DAC_CHANNEL_1);
    HAL_DAC_Start_DMA(&hdac,
                      DAC_CHANNEL_1,
                      (uint32_t*)dma_buf,
                      DMA_BUF_SIZE * 2,
                      DAC_ALIGN_12B_R);
    playback_started = 1;
}





/* USER CODE END 1 */
