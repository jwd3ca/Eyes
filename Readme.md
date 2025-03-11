# Configuration

1.  Copy `config.h.example` to `config.h`.
2.  Open `config.h` and replace the placeholder values with your actual credentials.

Code to use cartoonish eyes to indicate the state of air qualtiy, temp and humidity.
Uses M5Stack PMSA003 unit sitting on the M5Stack SHT30 base stand.

Uses wifi/influxdb to post data to local influxdb server which is then read by local grafana server
to display historical, as well as current, data. localhost:3000

