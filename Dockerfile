# AgentLight — сторінка налаштування WiFi лампи по Bluetooth.
# Одна сторінка, нічого більше.
#
# Хоститься по HTTPS тому, що Web Bluetooth працює лише в secure context
# (https або localhost) — з http-сторінки браузер не дасть доступу до Bluetooth.
# Сторінка самої лампи локальна: її віддає лампа по своїй IP.
FROM nginx:alpine

COPY web/setup.html /usr/share/nginx/html/index.html
COPY nginx/default.conf /etc/nginx/conf.d/default.conf

EXPOSE 80
