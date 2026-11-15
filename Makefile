PY ?= python

.PHONY: install check plan cost firmware test-firmware

install:  ## Python packages for the cloud checks
	$(PY) -m pip install -r cloud/requirements-dev.txt

check:  ## everything that needs no hardware and no AWS account
	$(PY) tools/check.py

plan:  ## synthesise the stack and list its resources; never deploys
	$(PY) tools/plan.py

cost:  ## monthly cost estimate at 10 / 100 / 1 000 scooters
	cd cloud && $(PY) cost.py

test-firmware:  ## the same host tests through PlatformIO + Unity
	cd firmware && pio test -e native

firmware:  ## compile the ESP32 firmware
	cd firmware && pio run -e esp32dev
