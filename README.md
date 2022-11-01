# elcoreclrun

Утилита elcorecl-run собирается в OpenWRT.
Для сборки требуются библиотеки [elcorecllib](https://github.com/dis-projects/elcorecllib)

Предполагается, что утилита будет расположена в директории
elv-openwrt/package/utils/elcoreclrun

Для добавления утилиты следует использовать make menuconfig
```
Utilities  --->
    <*> elcoreclrun.................................... Simple elcore run utility
```
