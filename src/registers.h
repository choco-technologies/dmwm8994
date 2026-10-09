#ifndef DMWM8994_REGISTERS_H
#define DMWM8994_REGISTERS_H

/* Register addresses from ST's wm8994_reg.h; values below follow wm8994.c.
 * https://github.com/STMicroelectronics/stm32-wm8994 */
#define WM_REG_RESET               0x0000U
#define WM_REG_POWER_1             0x0001U
#define WM_REG_POWER_2             0x0002U
#define WM_REG_POWER_4             0x0004U
#define WM_REG_POWER_5             0x0005U
#define WM_REG_HEADPHONE_LEFT      0x001CU
#define WM_REG_HEADPHONE_RIGHT     0x001DU
#define WM_REG_OUTPUT_MIXER_LEFT   0x002DU
#define WM_REG_OUTPUT_MIXER_RIGHT  0x002EU
#define WM_REG_ANTIPOP_2           0x0039U
#define WM_REG_WRITE_SEQUENCER     0x0110U
#define WM_REG_AIF1_CLOCKING       0x0200U
#define WM_REG_CORE_CLOCKING       0x0208U
#define WM_REG_AIF1_RATE           0x0210U
#define WM_REG_AIF1_CONTROL        0x0300U
#define WM_REG_AIF1_MASTER         0x0302U
#define WM_REG_ADC2_LEFT_VOLUME    0x0404U
#define WM_REG_ADC2_RIGHT_VOLUME   0x0405U
#define WM_REG_DAC1_LEFT_VOLUME    0x0402U
#define WM_REG_DAC1_RIGHT_VOLUME   0x0403U
#define WM_REG_ADC2_FILTER         0x0411U
#define WM_REG_DAC1_FILTER         0x0420U
#define WM_REG_DRC2                0x0450U
#define WM_REG_DAC1_LEFT_ROUTE     0x0601U
#define WM_REG_DAC1_RIGHT_ROUTE    0x0602U
#define WM_REG_DAC2_LEFT_ROUTE     0x0604U
#define WM_REG_DAC2_RIGHT_ROUTE    0x0605U
#define WM_REG_ADC2_LEFT_ROUTE     0x0608U
#define WM_REG_ADC2_RIGHT_ROUTE    0x0609U
#define WM_REG_DAC1_LEFT_GAIN      0x0610U
#define WM_REG_DAC1_RIGHT_GAIN     0x0611U
#define WM_REG_OVERSAMPLING        0x0620U
#define WM_REG_GPIO1               0x0700U

/* Undocumented registers in ST's codec startup workaround. */
#define WM_REG_STARTUP_WORKAROUND_1 0x0102U
#define WM_REG_STARTUP_WORKAROUND_2 0x0817U

#define WM_HEADPHONE_GAIN_MAX      0x003FU
#define WM_HEADPHONE_UNMUTE        0x0040U
#define WM_HEADPHONE_UPDATE        0x0100U
#define WM_DAC1_MUTE               0x0200U
#define WM_DAC1_FILTER_ENABLE      0x0010U
#define WM_ADC_VOLUME_UPDATE       0x0100U
#define WM_ADC_UNITY_GAIN          0x00C0U
#define WM_DAC_UNITY_GAIN          0x00C0U
#define WM_POWER_BIAS_VMID         0x0003U
#define WM_POWER_BIAS_VMID_MICBIAS 0x0013U
#define WM_POWER_DAC1_AIF1         0x0303U
#define WM_ROUTE_AIF1_TO_DAC1      0x0001U
#define WM_ROUTE_DMIC2_TO_ADC2     0x0002U
#define WM_OUTPUT_DAC1_TO_HP       0x0100U
#define WM_CORE_AIF1_DSP_CLOCKS    0x000AU
#define WM_AIF1_MCLK1_ENABLE       0x0001U
#define WM_STARTUP_WORKAROUND_ON   0x0003U
#define WM_ANTIPOP_VMID_RAMP       0x006CU
#define WM_WRITE_SEQ_START         0x8100U
#define WM_DMIC2_POWER             0x0C30U
#define WM_DMIC2_DRC               0x00DBU
#define WM_DMIC2_THERMAL           0x6000U
#define WM_DMIC2_GPIO1             0x000EU
#define WM_ADC_OVERSAMPLING        0x0002U
#define WM_ADC2_VOICE_FILTER       0x3800U

#define WM_AIF1_RATE_8000         0x0003U
#define WM_AIF1_RATE_11025        0x0013U
#define WM_AIF1_RATE_16000        0x0033U
#define WM_AIF1_RATE_22050        0x0043U
#define WM_AIF1_RATE_32000        0x0063U
#define WM_AIF1_RATE_44100        0x0073U
#define WM_AIF1_RATE_48000        0x0083U
#define WM_AIF1_RATE_96000        0x00A3U

#endif
