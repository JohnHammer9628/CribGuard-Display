How to use it

Copy to each Pi as pi-wifi-link.sh, if needed then:

	chmod +x pi-wifi-link.sh

On Pi #1 (the AP):

	sudo SSID=Crib-Guard PSK='password' ./pi-wifi-link.sh ap

On Pi #2 (the client):

	sudo SSID=Crib-Guard PSK='password' ./pi-wifi-link.sh client

On your laptop, join Wi-Fi Pi-Network, then:

	ssh pi@192.168.50.1
	ssh pi@192.168.50.2