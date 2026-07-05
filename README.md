# tempchart
display chart of temperature on eink

## 效果预览
![温度图表](assets/1_tempchart.png)

## test OTA
- 1. PC side server tool inside 'tools' folder of this project;
- 2. copy the latest fw bin to server folder;
- 3. run '''python ota_https_server.py ./ 8070'''
- 4. device side, connect to wifi, then input ota command in console;