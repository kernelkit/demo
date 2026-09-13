SUBDIRS = classic breeze

all clean fmt:
	@for dir in $(SUBDIRS); do \
		if [ -d "$$dir" ]; then \
			$(MAKE) -C $$dir $@ || exit 1; \
		fi; \
	done

.PHONY: all clean fmt
