##
## This file is part of the sigrok-firmware project.
##
## Copyright (C) 2012 Uwe Hermann <uwe@hermann-uwe.de>
## Copyright (C) 2012 Bert Vermeulen <bert@biot.com>
##
## This program is free software; you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation; either version 2 of the License, or
## (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; if not, see <http://www.gnu.org/licenses/>.
##

GNUMAKEFLAGS = --no-print-directory

dist_pkgdata_DATA = \
	asix-sigma-phasor.fw \
	kingst-la-01a2.fw \
	kingst-la1016-fpga.bitstream \
	kingst-la1016a1-fpga.bitstream \
	kingst-la2016-fpga.bitstream \
	kingst-la2016a1-fpga.bitstream \
	fx2lafw-braintechnology-usb-lps.fw \
	fx2lafw-cwav-usbeeax.fw \
	fx2lafw-cwav-usbeedx.fw \
	fx2lafw-cwav-usbeesx.fw \
	fx2lafw-cwav-usbeezx.fw \
	fx2lafw-cypress-fx2.fw \
	fx2lafw-hantek-6022be.fw \
	fx2lafw-hantek-6022bl.fw \
	fx2lafw-instrustar-isds205b.fw \
	fx2lafw-sainsmart-dds120.fw \
	fx2lafw-saleae-logic.fw \
	fx2lafw-sigrok-fx2-16ch.fw \
	fx2lafw-sigrok-fx2-8ch.fw \
	fx2lafw-usb-c-grok.fw \
	fx2lafw-yixingdianzi-mdso.fw \
	hantek-dso-2090.fw \
	hantek-dso-2150.fw \
	hantek-dso-2250.fw \
	hantek-dso-5200.fw \
	hantek-dso-5200A.fw \
	saleae-logic16-fpga-18.bitstream \
	saleae-logic16-fpga-33.bitstream \
	saleae-logic16-fx2.fw \
	saleae-logicpro16-fpga.bitstream \
	saleae-logicpro16-fx3.fw \
	asix-sigma/asix-sigma-100.fw \
	asix-sigma/asix-sigma-200.fw \
	asix-sigma/asix-sigma-50.fw \
	asix-sigma/asix-sigma-50sync.fw \
	dreamsourcelab-dslogic/DSLogic.fw \
	dreamsourcelab-dslogic/DSLogicBasic.fw \
	dreamsourcelab-dslogic/DSLogicPlus.fw \
	dreamsourcelab-dslogic/DSLogicPro.fw \
	dreamsourcelab-dslogic/DSLogic33.bin \
	dreamsourcelab-dslogic/DSLogic50.bin \
	dreamsourcelab-dslogic/DSLogicBasic.bin \
	dreamsourcelab-dslogic/DSLogicPlus-pgl12-2.bin \
	dreamsourcelab-dslogic/DSLogicPlus-pgl12.bin \
	dreamsourcelab-dslogic/DSLogicPlus.bin \
	dreamsourcelab-dslogic/DSLogicPro.bin \
	dreamsourcelab-dslogic/DSLogicU2Basic-pgl12-2.bin \
	dreamsourcelab-dslogic/DSLogicU2Basic-pgl12.bin \
	dreamsourcelab-dslogic/DSLogicU2Basic.bin \
	dreamsourcelab-dslogic/DSLogicU2Pro16.bin \
	dreamsourcelab-dslogic/DSLogicU3Pro16.bin \
	dreamsourcelab-dslogic/DSLogicU3Pro32.bin \
	sysclk-lwla/sysclk-lwla1034-extneg.rbf \
	sysclk-lwla/sysclk-lwla1034-extpos.rbf \
	sysclk-lwla/sysclk-lwla1034-int.rbf \
	sysclk-lwla/sysclk-lwla1034-off.rbf

dist_noinst_DATA = \
	asix-sigma/LICENSE.Sigma \
	dreamsourcelab-dslogic/licenses/license.txt \
	sysclk-lwla/LICENSE.LWLA

MAINTAINERCLEANFILES = ChangeLog

.PHONY: ChangeLog
ChangeLog:
	git --git-dir '$(top_srcdir)/.git' log >$@ || touch $@

dist-hook: ChangeLog
