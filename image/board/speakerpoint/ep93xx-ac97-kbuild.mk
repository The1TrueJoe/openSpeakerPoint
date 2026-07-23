
snd-soc-ep93xx-ac97-y           := ep93xx-ac97.o
snd-soc-speakerpoint-y          := speakerpoint-card.o
obj-$(CONFIG_SND_EP93XX_SOC_AC97)       += snd-soc-ep93xx-ac97.o
obj-$(CONFIG_SND_SOC_SPEAKERPOINT)      += snd-soc-speakerpoint.o
