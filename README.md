# ESP32 GSM QUIZ

A mini project to get familiar with ESP-IDF and learn how GSM modem and DAC works. The main idea is simple, ESP32 is connected to GSM modem(tested on SIM800L, but probably will work with other similar modems), ESP32 will control GSM modem using AT commands. Calls to the GSM modem will be automatically accepted and then an audio with quiz questions will be played to the microphone input of the GSM modem, the caller will listen the question and then answer by pressing a keypad number. When caller answered all questions the final audio file will be chosen depending on accumulated points and played, then the call will stopped. The 8 bit DAC of ESP32 is not enough to play audio to microphone input of GSM modem, so external DAC(PCM5102 in my case) was used, the audio data to the DAC was transfered using I2S.

### Telegram bot
ESP32 can be controlled by telegram bot. For example firmware can be updated using the bot, and also the question data for the quiz updated in this way.

### Question data
To build a file which will contain mp3 data for questions of the quiz "bundle_generator.py" can be used, it was written in python3. The file that was generated then should be uploaded using telegram bot. 

### Audio
All audio should be in mp3 format, also because GSM have low bitrate there is no reason to use high quality audio because the caller won't be able to hear it anyway. To decode mp3 on ESP32 a header-only library minimp3 was used. 
