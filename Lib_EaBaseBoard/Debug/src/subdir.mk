################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/acc.c \
../src/eeprom.c \
../src/flash.c \
../src/font5x7.c \
../src/joystick.c \
../src/led7seg.c \
../src/light.c \
../src/oled.c \
../src/pca9532.c \
../src/rgb.c \
../src/rotary.c \
../src/temp.c \
../src/uart2.c 

C_DEPS += \
./src/acc.d \
./src/eeprom.d \
./src/flash.d \
./src/font5x7.d \
./src/joystick.d \
./src/led7seg.d \
./src/light.d \
./src/oled.d \
./src/pca9532.d \
./src/rgb.d \
./src/rotary.d \
./src/temp.d \
./src/uart2.d 

OBJS += \
./src/acc.o \
./src/eeprom.o \
./src/flash.o \
./src/font5x7.o \
./src/joystick.o \
./src/led7seg.o \
./src/light.o \
./src/oled.o \
./src/pca9532.o \
./src/rgb.o \
./src/rotary.o \
./src/temp.o \
./src/uart2.o 


# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.c src/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: MCU C Compiler'
	arm-none-eabi-gcc -DDEBUG -D__CODE_RED -D__REDLIB__ -I"C:\Users\student\Documents\SQL Class\Odtwarzacz-MP3-main\Lib_CMSISv1p30_LPC17xx\inc" -I"C:\Users\student\Documents\SQL Class\Odtwarzacz-MP3-main\Lib_EaBaseBoard\inc" -I"C:\Users\student\Documents\SQL Class\Odtwarzacz-MP3-main\Lib_MCU\inc" -O0 -g3 -gdwarf-4 -Wall -c -fmessage-length=0 -fno-builtin -ffunction-sections -fmerge-constants -fmacro-prefix-map="$(<D)/"= -mcpu=cortex-m3 -mthumb -D__REDLIB__ -fstack-usage -specs=redlib.specs -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.o)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-src

clean-src:
	-$(RM) ./src/acc.d ./src/acc.o ./src/eeprom.d ./src/eeprom.o ./src/flash.d ./src/flash.o ./src/font5x7.d ./src/font5x7.o ./src/joystick.d ./src/joystick.o ./src/led7seg.d ./src/led7seg.o ./src/light.d ./src/light.o ./src/oled.d ./src/oled.o ./src/pca9532.d ./src/pca9532.o ./src/rgb.d ./src/rgb.o ./src/rotary.d ./src/rotary.o ./src/temp.d ./src/temp.o ./src/uart2.d ./src/uart2.o

.PHONY: clean-src

