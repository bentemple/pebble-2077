const Clay = require('pebble-clay')
const clayConfig = require('./config')

// Cached settings from Clay
let settings = {
    skipLocation: false  // Default
}

function loadSettings() {
    try {
        const stored = localStorage.getItem('clay-settings')
        if (stored) {
            const parsed = JSON.parse(stored)
            settings.skipLocation = parsed.PREF_SKIP_LOCATION !== undefined
                ? parsed.PREF_SKIP_LOCATION
                : true
        }
    } catch (e) {
        console.log('Error loading settings: ' + e.message)
    }
}

function customClay() {
    this.on(this.EVENTS.AFTER_BUILD, function() {
        const topTextSelect = this.getItemById('top-text-select')
        const topTextInput = this.getItemById('top-text-input')

        topTextSelect.on('change', function() {
            topTextInput.set(topTextSelect.get())
        })

        const bottomTextSelect = this.getItemById('bottom-text-select')
        const bottomTextInput = this.getItemById('bottom-text-input')

        bottomTextSelect.on('change', function() {
            bottomTextInput.set(bottomTextSelect.get())
        })
    }.bind(this))
}

const clay = new Clay(clayConfig, customClay)

function getWeatherDescription(code) {
  const map = {
    0: "CLEAR",
    1: "CLEAR-",
    2: "PRT_CLOUDY",
    3: "OVERCAST",
    45: "FOG",
    48: "FOG",
    51: "DRIZZLE-",
    53: "DRIZZLE",
    55: "DRIZZLE+",
    56: "FRZ_DRIZ-",
    57: "FRZ_DRIZ+",
    61: "RAIN-",
    63: "RAIN",
    65: "RAIN+",
    66: "FRZ_RAIN-",
    67: "FRZ_RAIN+",
    71: "SNOW-",
    73: "SNOW",
    75: "SNOW+",
    77: "SNOW_GRAIN",
    80: "SHOWERS-",
    81: "SHOWERS",
    82: "SHOWERS+",
    85: "SNW_SHOWER-",
    86: "SNW_SHOWER+",
    95: "THNDRSTRM",
    96: "STORM_HAIL-",
    99: "STORM_HAIL+"
  };

  return map.hasOwnProperty(code)
    ? map[code]
    : `UNKNOWN: ${code}`;
}

const xhrRequest = function (url, type, callback) {
    let xhr = new XMLHttpRequest();
    xhr.onload = function () {
    callback(this.responseText);
    };
    xhr.open(type, url);
    xhr.send();
};

function fetchWeatherWithCoords(lat, lon) {
    const url = `https://api.open-meteo.com/v1/forecast?latitude=${lat}&longitude=${lon}&current_weather=true&daily=temperature_2m_max&timezone=auto`

    xhrRequest(url, 'GET',
        (res) => {
            const data = JSON.parse(res)
            const temp = Math.round(data.current_weather.temperature)
            const tempHigh = Math.round(data.daily.temperature_2m_max[0])
            const weather_code = parseInt(data.current_weather.weathercode)
            const conditions = getWeatherDescription(weather_code)

            const dictionary = {
                'TEMPERATURE': temp,
                'TEMPERATURE_HIGH': tempHigh,
                'CONDITIONS': conditions
            }

            Pebble.sendAppMessage(dictionary,
                () => {
                    console.log('Weather sent to Pebble')
                },
                () => {
                    console.log('Error sending weather to Pebble')
                })
        }
    )
}

function getWeather() {
    if (settings.skipLocation) {
        // Use 0,0 - Gadgetbridge or similar will intercept
        fetchWeatherWithCoords(0, 0)
    } else {
        // Try to get real location
        navigator.geolocation.getCurrentPosition(
            (pos) => {
                fetchWeatherWithCoords(pos.coords.latitude, pos.coords.longitude)
            },
            (err) => {
                console.log('Geolocation failed: ' + err.message)
                // Don't fetch weather if location fails
            },
            { timeout: 15000, maximumAge: 60000 }
        )
    }
}

Pebble.addEventListener('ready',
    () => {
        loadSettings()
        getWeather()
    }
)

Pebble.addEventListener('appmessage',
    (e) => {
        console.log('appmessage received: ' + JSON.stringify(e.payload))
        loadSettings()
        getWeather()
    }
)

