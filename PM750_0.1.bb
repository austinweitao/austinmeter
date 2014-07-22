DESCRIPTION = "This package contains the simple PM750 test program."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COREBASE}/LICENSE;md5=3f40d7994397109285ec7b81fdeb3b58 \
                    file://${COREBASE}/meta/COPYING.MIT;md5=3da9cfbcb788c80a0384361b4de20420"

PR = "r1"

SRC_URI = "file://timer2.c \
	   file://unit-test.h  \
	   file://sll.h  \
	   file://sll.c  \
	   file://ftpupload.c  \
	   file://uci_test.c  \
	   file://uci_test1.c  \
	   file://append.c  \
	   file://sampling.c  \
          "

S = "${WORKDIR}"

do_compile() {
  ${CC} ${CFLAGS} -o timer2 timer2.c sll.c -lmodbus -lrt -lcurl
  ${CC} ${CFLAGS} -o sampling sampling.c sll.c -lmodbus -lrt -lcurl -luci
  ${CC} ${CFLAGS} -o append append.c sll.c -luci
  ${CC} ${CFLAGS} -o ftpupload ftpupload.c -lcurl
  ${CC} ${CFLAGS} -o uci_test uci_test.c -luci
  ${CC} ${CFLAGS} -o uci_test1 uci_test1.c -luci
}

do_install() {
  install -d ${D}${bindir}
  install -m 0755 timer2 ${D}${bindir}
  install -m 0755 sampling ${D}${bindir}
  install -m 0755 ftpupload ${D}${bindir}
  install -m 0755 append ${D}${bindir}
  install -m 0755 uci_test ${D}${bindir}
  install -m 0755 uci_test1 ${D}${bindir}
}
