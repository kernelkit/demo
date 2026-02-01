SUBDIRS = classic breeze

all clean:
	@for dir in $(SUBDIRS); do \
		if [ -d "$$dir" ]; then \
			$(MAKE) -C $$dir $@ || exit 1; \
		fi; \
	done

.PHONY: all clean
