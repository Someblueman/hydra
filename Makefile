# Makefile for Hydra
# POSIX-compliant build and lint tasks

.PHONY: all lint test clean install help

# Default target
all: lint

# Lint all shell scripts for POSIX compliance
lint:
	@echo "Running ShellCheck for POSIX compliance..."
	@find . -name "*.sh" -o -path "./bin/hydra" | while read -r file; do \
		echo "Checking $$file..."; \
		shellcheck --shell=sh --severity=style "$$file" || exit 1; \
	done
	@echo "Running dash syntax check..."
	@find . -name "*.sh" -o -path "./bin/hydra" | while read -r file; do \
		echo "Validating $$file..."; \
		dash -n "$$file" || exit 1; \
	done
	@echo "All checks passed!"

# Run tests
test:
	@echo "Running tests..."
	@if [ -d tests ] && [ -n "$$(ls -A tests/*.sh 2>/dev/null)" ]; then \
		for test in tests/*.sh; do \
			echo "Running $$test..."; \
			sh "$$test" || exit 1; \
		done; \
	else \
		echo "No tests found in tests/"; \
	fi

# Clean temporary files
clean:
	@echo "Cleaning temporary files..."
	@find . -name "*~" -o -name "*.swp" -o -name ".*.swp" | xargs rm -f
	@echo "Clean complete"

# Install hydra to /usr/local/bin
install:
	@echo "Installing hydra..."
	@mkdir -p /usr/local/bin
	@cp bin/hydra /usr/local/bin/hydra
	@chmod +x /usr/local/bin/hydra
	@mkdir -p /usr/local/lib/hydra
	@cp lib/*.sh /usr/local/lib/hydra/
	@echo "Installation complete"
	@echo "Run 'hydra help' to get started"

# Display help
help:
	@echo "Hydra Makefile targets:"
	@echo "  make lint    - Run ShellCheck and dash syntax validation"
	@echo "  make test    - Run all tests"
	@echo "  make clean   - Remove temporary files"
	@echo "  make install - Install hydra to /usr/local/bin"
	@echo "  make help    - Show this help message"