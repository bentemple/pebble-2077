const config = [
    {
        "type": "heading",
        "defaultValue": "2077"
    },
    {
        "type": "section",
        "capabilities": ["HEALTH"],
        "items": [
            {
                "type": "heading",
                "defaultValue": "Health"
            },
            {
                "type": "toggle",
                "messageKey": "PREF_SHOW_STEPS",
                "label": "Show step count",
                "defaultValue": true
            }
        ]
    },
    {
        "type": "section",
        "items": [
            {
                "type": "heading",
                "defaultValue": "Progress Bar"
            },
            {
                "type": "select",
                "messageKey": "PREF_PROGRESS_MODE",
                "label": "Display",
                "defaultValue": "2",
                "options": [
                    {
                        "value": "0",
                        "label": "Battery"
                    },
                    {
                        "value": "1",
                        "label": "Steps (toward goal)"
                    },
                    {
                        "value": "2",
                        "label": "Sleep (toward goal)"
                    }
                ]
            },
            {
                "type": "input",
                "messageKey": "PREF_STEP_GOAL",
                "label": "Step goal",
                "defaultValue": "10000",
                "attributes": {
                    "type": "number"
                }
            },
            {
                "type": "input",
                "messageKey": "PREF_SLEEP_GOAL",
                "label": "Sleep goal (minutes)",
                "defaultValue": "420",
                "attributes": {
                    "type": "number"
                }
            }
        ]
    },
    {
        "type": "section",
        "items": [
            {
                "type": "heading",
                "defaultValue": "Weather"
            },
            {
                "type": "text",
                "defaultValue": "Weather data is sourced from Open-Meteo."
            },
            {
                "type": "toggle",
                "messageKey": "PREF_SHOW_WEATHER",
                "label": "Show current weather",
                "defaultValue": true
            },
            {
                "type": "toggle",
                "messageKey": "PREF_WEATHER_METRIC",
                "label": "Use metric units (Celsius)",
                "defaultValue": true
            },
            {
                "type": "toggle",
                "messageKey": "PREF_SKIP_LOCATION",
                "label": "Skip location (use 0,0)",
                "defaultValue": true
            },
            {
                "type": "text",
                "defaultValue": "Enable 'Skip location' if using Gadgetbridge or another app that intercepts weather requests. Disable to use real GPS location."
            }
        ]
    },
    {
        "type": "section",
        "items": [
            {
                "type": "heading",
                "defaultValue": "Alerts"
            },
            {
                "type": "toggle",
                "messageKey": "PREF_HOUR_VIBE",
                "label": "Hourly vibration",
                "defaultValue": false
            },
            {
                "type": "toggle",
                "messageKey": "PREF_DISCONNECT_ALERT",
                "label": "Disconnect alert",
                "defaultValue": true
            }
        ]
    },
    {
        "type": "section",
        "items": [
            {
                "type": "heading",
                "defaultValue": "Top Text Customization"
            },
            {
                "type": "select",
                "id": "top-text-select",
                "label": "Presets",
                "defaultValue": "",
                "options": [
                    {
                        "value": "",
                        "label": "None"
                    },
                    {
                        "value": "PBL_%m%U%j",
                        "label": "Default: Month, week number, and day of year"
                    },
                    {
                        "value": "%Y_%b",
                        "label": "Year and short month"
                    },
                    {
                        "value": "%B",
                        "label": "Full month"
                    },
                    {
                        "value": "%G_%V",
                        "label": "ISO 8601 year and week number"
                    },
                    {
                        "value": "UPTIME_$U",
                        "label": "Uptime since wake (HH:MM)"
                    }
                ]
            },
            {
                "type": "input",
                "id": "top-text-input",
                "messageKey": "PREF_CUSTOM_TEXT",
                "label": "Text",
                "defaultValue": "PBL_%m%U%j",
                "attributes": {
                    "limit": 16
                }
            },
            {
                "type": "text",
                "defaultValue": "Text can be formatted with the current time by following the <a href=\"https://man7.org/linux/man-pages/man3/strftime.3.html\">strftime(3) manpage</a>. Some common examples:<ul><li>%b - abbreviated month name</li><li>%B - full month name</li><li>%j - day of the year</li><li>%m - month</li><li>%u - day of the week as a number</li><li>%U - week number</li><li>%y - two digit year</li><li>%Y - full year</li></ul><br>Special: <b>$U</b> - uptime since wake (HH:MM)"
            }
        ]
    },
    {
        "type": "section",
        "items": [
            {
                "type": "heading",
                "defaultValue": "Bottom Text Customization"
            },
            {
                "type": "select",
                "id": "bottom-text-select",
                "label": "Presets",
                "defaultValue": "",
                "options": [
                    {
                        "value": "",
                        "label": "None"
                    },
                    {
                        "value": "%Y.%m.%d",
                        "label": "Default: YYYY.MM.DD"
                    },
                    {
                        "value": "%Y-%m-%d",
                        "label": "ISO date: YYYY-MM-DD"
                    },
                    {
                        "value": "%b %d",
                        "label": "Month and day"
                    },
                    {
                        "value": "UPTIME_$U",
                        "label": "Uptime since wake (HH:MM)"
                    }
                ]
            },
            {
                "type": "input",
                "id": "bottom-text-input",
                "messageKey": "PREF_BOTTOM_TEXT",
                "label": "Text",
                "defaultValue": "%Y.%m.%d",
                "attributes": {
                    "limit": 16
                }
            }
        ]
    },
    {
        "type": "submit",
        "defaultValue": "Save"
    }
]

module.exports = config