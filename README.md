# Embedded System Exam 1

## (1) how to setup and run my program

1. 首先我先遵照題目的前兩點：

```
1. mbed run the RPC loop to get input string commands from PC/Python, and it will in turn call custom RPC functions.
2. PC/Python use RPC over serial to send a command to call accelerator capture mode on mbed
```

在 main function 中加入吃RPC指令的code，如下圖：

![](https://i.imgur.com/YKOId8z.png)

再來，我定義了我主要實作capture gesture 和 feature 的 RPC function `gesture`，當輸入RPC指令“gesture”之後便會進來這裡:

![](https://i.imgur.com/susoTnT.png)

其中我會先對姿勢做 classification，在判斷現在的傾角和reference傾角的差距的絕對值，如果超過15度則feature value為1，否則為0，拿gesture ID為0的code來當範例，獲得ID和feature value後，我就把他們各自存到一個array裡面，方便之後讀取。

![](https://i.imgur.com/oZ7TIC5.png)

![](https://i.imgur.com/lfhPc0c.png)

在獲得10個value之後，我的python就會傳指令回來讓他回到RPC loop。

![](https://i.imgur.com/sjiQa8S.png)

然後依照題目，我們要再輸入指令去看看剛剛存到array裡面的gesture ID 和 feature value為多少，所以我定義另一個RPC function，當輸入"get_value"時就可以看看那些value。

![](https://i.imgur.com/bbkuSQr.png)

![](https://i.imgur.com/dyYEfQq.png)


## (2) what are the results

1. 量測 reference angle

![](https://i.imgur.com/NreGqte.png)

2. 10次之後就返回

![](https://i.imgur.com/RHsd4Uk.png)

3. 輸入 get_value

![](https://i.imgur.com/RiSnV6z.png)
