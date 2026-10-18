.PHONY: all build check test
all: build
build:
	./platform/3ds/build.sh
check:
	python3 scripts/check-repository.py
	python3 scripts/dev/check.py
test:
	python3 scripts/dev/run-tests.py
