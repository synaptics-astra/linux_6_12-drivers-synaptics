# SPDX-License-Identifier: GPL-2.0
#
# Synaptics drivers
#

obj-$(CONFIG_SYNAPTICS_CLK)			+= clk/

obj-$(CONFIG_SYNAPTICS_PINCTRL)		+= pinctrl/

obj-$(CONFIG_SYNAPTICS_MMC)			+= mmc/

obj-$(CONFIG_SYNAPTICS_USB)			+= usb/

obj-$(CONFIG_SYNAPTICS_PCIE)			+= pci/

obj-$(CONFIG_SYNAPTICS_REGULATOR)	+= regulator/

obj-$(CONFIG_SYNAPTICS_PHY)			+= phy/

obj-$(CONFIG_SYNAPTICS_HWMON)		+= hwmon/

obj-$(CONFIG_SYNAPTICS_IIO)			+= iio/

obj-$(CONFIG_SYNAPTICS_I2C)			+= i2c/

obj-$(CONFIG_SYNAPTICS_SOC)			+= soc/

obj-$(CONFIG_SYNAPTICS_NET)			+= net/

obj-$(CONFIG_SYNAPTICS_VIDEO)		+= video/

obj-$(CONFIG_SYNAPTICS_SOUND)		+= sound/

obj-$(CONFIG_SYNAPTICS_DEVFREQ)		+= devfreq/

obj-$(CONFIG_SYNAPTICS_DMABUF)		+= dma-buf/

obj-$(CONFIG_SYNAPTICS_IRQCHIP)		+= irqchip/

obj-$(CONFIG_SYNAPTICS_INPUT)		+= input/

obj-$(CONFIG_SYNAPTICS_BLUETOOTH)	+= bluetooth/

obj-$(CONFIG_SYNAPTICS_CLOCKSOURCE)	+= clocksource/

obj-$(CONFIG_SYNAPTICS_DMA)			+= dma/

obj-$(CONFIG_MALI_MIDGARD)              += gpu/arm/mali-midgard/

obj-$(CONFIG_SYNAPTICS_TTY)			+= tty/

obj-$(CONFIG_SYNAPTICS_LEDS)		+= leds/

obj-$(CONFIG_SYNAPTICS_MFD)			+= mfd/

obj-$(CONFIG_SYNAPTICS_MISC)		+= misc/

obj-$(CONFIG_SYNAPTICS_MTD)			+= mtd/

obj-$(CONFIG_SYNAPTICS_PWM)			+= pwm/

obj-$(CONFIG_SYNAPTICS_RESET)		+= reset/

obj-$(CONFIG_SYNAPTICS_RTC)			+= rtc/

obj-$(CONFIG_SYNAPTICS_SPI)			+= spi/

obj-$(CONFIG_SYNAPTICS_STAGING)		+= staging/

obj-$(CONFIG_SYNAPTICS_WATCHDOG)	+= watchdog/

obj-$(CONFIG_SYNAPTICS_MAILBOX)		+= mailbox/

obj-$(CONFIG_SYNAPTICS_RPMSG)		+= rpmsg/

obj-$(CONFIG_SYNAPTICS_HRX_V4L2)	+= hrx-v4l2/

obj-y	+= drm/

obj-$(CONFIG_SYNAPTICS_CRYPTO)	+= crypto/
