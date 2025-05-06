################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/libhelix/real/bitstream.c \
../src/libhelix/real/buffers.c \
../src/libhelix/real/dct32.c \
../src/libhelix/real/dequant.c \
../src/libhelix/real/dqchan.c \
../src/libhelix/real/huffman.c \
../src/libhelix/real/hufftabs.c \
../src/libhelix/real/imdct.c \
../src/libhelix/real/polyphase.c \
../src/libhelix/real/scalfact.c \
../src/libhelix/real/stproc.c \
../src/libhelix/real/subband.c \
../src/libhelix/real/trigtabs.c 

C_DEPS += \
./src/libhelix/real/bitstream.d \
./src/libhelix/real/buffers.d \
./src/libhelix/real/dct32.d \
./src/libhelix/real/dequant.d \
./src/libhelix/real/dqchan.d \
./src/libhelix/real/huffman.d \
./src/libhelix/real/hufftabs.d \
./src/libhelix/real/imdct.d \
./src/libhelix/real/polyphase.d \
./src/libhelix/real/scalfact.d \
./src/libhelix/real/stproc.d \
./src/libhelix/real/subband.d \
./src/libhelix/real/trigtabs.d 

OBJS += \
./src/libhelix/real/bitstream.o \
./src/libhelix/real/buffers.o \
./src/libhelix/real/dct32.o \
./src/libhelix/real/dequant.o \
./src/libhelix/real/dqchan.o \
./src/libhelix/real/huffman.o \
./src/libhelix/real/hufftabs.o \
./src/libhelix/real/imdct.o \
./src/libhelix/real/polyphase.o \
./src/libhelix/real/scalfact.o \
./src/libhelix/real/stproc.o \
./src/libhelix/real/subband.o \
./src/libhelix/real/trigtabs.o 


# Each subdirectory must supply rules for building sources it contributes
src/libhelix/real/%.o: ../src/libhelix/real/%.c src/libhelix/real/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: MCU C Compiler'
	arm-none-eabi-gcc -DDEBUG -D__CODE_RED -DCORE_M3 -D__LPC17XX__ -D__NEWLIB__ -I"C:\Users\student\Documents\SQL Class\Odtwarzacz-MP3-main\Helix\inc" -O0 -fno-common -g3 -gdwarf-4 -Wall -c -fmessage-length=0 -fno-builtin -ffunction-sections -fdata-sections -fmerge-constants -fmacro-prefix-map="$(<D)/"= -mcpu=cortex-m3 -mthumb -fstack-usage -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.o)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-src-2f-libhelix-2f-real

clean-src-2f-libhelix-2f-real:
	-$(RM) ./src/libhelix/real/bitstream.d ./src/libhelix/real/bitstream.o ./src/libhelix/real/buffers.d ./src/libhelix/real/buffers.o ./src/libhelix/real/dct32.d ./src/libhelix/real/dct32.o ./src/libhelix/real/dequant.d ./src/libhelix/real/dequant.o ./src/libhelix/real/dqchan.d ./src/libhelix/real/dqchan.o ./src/libhelix/real/huffman.d ./src/libhelix/real/huffman.o ./src/libhelix/real/hufftabs.d ./src/libhelix/real/hufftabs.o ./src/libhelix/real/imdct.d ./src/libhelix/real/imdct.o ./src/libhelix/real/polyphase.d ./src/libhelix/real/polyphase.o ./src/libhelix/real/scalfact.d ./src/libhelix/real/scalfact.o ./src/libhelix/real/stproc.d ./src/libhelix/real/stproc.o ./src/libhelix/real/subband.d ./src/libhelix/real/subband.o ./src/libhelix/real/trigtabs.d ./src/libhelix/real/trigtabs.o

.PHONY: clean-src-2f-libhelix-2f-real

