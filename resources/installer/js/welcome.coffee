#Copyright (c) 2011 ~ 2013 Deepin, Inc.
#              2011 ~ 2013 yilang
#
#Author:      LongWei <yilang2007lw@gmail.com>
#Maintainer:  LongWei <yilang2007lw@gmail.com>
#
#This program is free software; you can redistribute it and/or modify
#it under the terms of the GNU General Public License as published by
#the Free Software Foundation; either version 3 of the License, or
#(at your option) any later version.
#
#This program is distributed in the hope that it will be useful,
#but WITHOUT ANY WARRANTY; without even the implied warranty of
#MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#GNU General Public License for more details.
#
#You should have received a copy of the GNU General Public License
#along with this program; if not, see <http://www.gnu.org/licenses/>.

__selected_layout = "es"
__selected_timezone = "Asia/Shanghai"

__database = JSON.parse timezone_json

class Keyboard extends Widget
    constructor: (@id)->
        super
        @current = create_element("div", "KeyBoardCurrent", @element)
        @current.innerText = __selected_layout

        @list = create_element("div", "KeyBoardList", @element)
        for layout in @get_layouts()
            @construct_item(layout)

    get_layouts: ->
        layouts = []
        for layout in DCore.Installer.get_keyboard_layouts()
            layouts.push(layout)
            for variant in DCore.Installer.get_layout_variants(layout)
                layouts.push(layout + "," + variant)
        return layouts

    construct_item: (layout) ->
        opt = create_element("div", "KeyboardItem", @list)
        opt.innerText = layout
        opt.addEventListener("click", (e) =>
            @update_layout(layout)
        )
        opt.addEventListener("mouseover", (e) =>
            echo "mouse over"
        )

    update_layout: (layout) ->
        @current.innerText = layout
        __selected_layout = layout

class Timezone extends Widget
    constructor: (@id) ->
        super
        @current = create_element("div", "TimezoneCurrent", @element)
        @current.innerText = __selected_timezone
        @picker_wrap = create_element("div","TmezoneWrap", @element)
        @picker = create_element("div", "TimezonePicker", @picker_wrap)
        @canvas = create_element("canvas", "TimezoneCanvas", @picker)
        @canvas.setAttribute("width", 736)
        @canvas.setAttribute("height", 404)
        @img = create_img("TimezoneMap", "images/zonemap.png", @picker)
        @img.setAttribute("usemap", "#ImageMap")
        @construct_map()

    update_timezone: (zone) ->
        @current.innerText = zone
        __selected_timezone = zone

    construct_map: ->
        @imagemap = create_element("map", "", @element)
        @imagemap.setAttribute("name", "ImageMap")
        @imagemap.setAttribute("id", "ImageMap")
        for key, val of __database
            @construct_area(key)

    construct_area: (key) ->
        area = create_element("area", "TimezoneArea", @imagemap)
        area.setAttribute("data-timezone", key)
        area.setAttribute("data-country", __database[key]["country"])
        area.setAttribute("data-pin", __database[key]["pin"])
        area.setAttribute("data-offset", __database[key]["offset"])
        area.setAttribute("href", "#")
        if __database[key]["polys"].length != 0
            area.setAttribute("shape", "poly")
            area.setAttribute("coords", __database[key]["polys"])
        else if __database[key]["rects"].length != 0
            area.setAttribute("shape", "rect")
            area.setAttribute("coords", __database[key]["rects"])
        area.addEventListener("click", (e) =>
            @show_pin(area)
            @update_timezone(area.getAttribute("data-timezone"))
        )
        area.addEventListener("mouseover", (e) =>
            @draw_timezone(area)
        )
        area.addEventListener("mouseout", (e) =>
            @destroy_canvas(area)
        )
    
    show_pin: (area) ->
        pin = area.getAttribute("data-pin").split(",")
        @pin?.parentElement?.removeChild(@pin)
        @pin = null
        @pin = create_img("", "images/pin.png", @picker_wrap)
        @pin.setAttribute("position", "absolute")
        @pin.setAttribute("left", pin[0] + "px")
        @pin.setAttribute("top", pin[1] + "px")
        @pin.setAttribute("width", "13px")
        @pin.setAttribute("height", "21px")

    draw_timezone: (myarea) ->
        offset = myarea.getAttribute("data-offset")
        for area in @get_offset_areas(offset)
            @draw_canvas(area)

    get_offset_areas: (offset) ->
        areas = []
        for area in @imagemap.children
            if area.getAttribute("data-offset") == offset
                areas.push(area)
        return areas

    draw_canvas: (area) ->
        ctx = @canvas.getContext("2d")
        poly = area.getAttribute("coords").split(",")
        ctx.beginPath()
        ctx.moveTo(poly[0],poly[1])
        i = 2
        while i < poly.length - 1
            ctx.lineTo(poly[i], poly[i+1])
            i = i + 2
        ctx.closePath()
        ctx.fillStyle = "#FF0000"
        ctx.fill()

    destroy_canvas: (area) ->
        ctx = @canvas.getContext("2d")
        ctx.clearRect(0,0,736,404)

class Welcome extends Page
    constructor: (@id)->
        super
        @illegal_keys='\t\n\r~`!@#$%^&*()+}{|\\\':;<,>.?/'
        @keyboard_displayed = false
        @timezone_displayed = false

        @title_start = create_element("div", "", @title)
        @start_txt = create_element("p", "", @title_start)
        @start_txt.innerText = "安装向导"

        @title_set = create_element("div", "TitleSet", @title)
        @keyboard_set = create_element("span", "KeyboardSet", @title_set)
        @keyboard_set.innerText = "键盘"
        @keyboard_set.addEventListener("click", (e) =>
            @display_keyboard()
        )
        @timezone_set = create_element("span", "TimezoneSet", @title_set)
        @timezone_set.innerText = "时区"
        @timezone_set.addEventListener("click", (e) =>
            @display_timezone()
        )

        @form = create_element("div", "WelcomeForm", @element)

        @username = create_element("div", "WelcomeFormItem", @form)
        @username_txt = create_element("div", "", @username)
        @username_txt.innerText = "用户名 :"
        @username_input = create_element("input", "", @username)
        @username_input.setAttribute("id", "WelcomeUsername")
        @username_input.setAttribute("placeholder", "2-14个字符，英文，数字，中文")
        @username_tip = create_element("div", "WelcomeTip", @username)
        @username_angle = create_element("span", "WelcomeAngle", @username_tip)
        @username_error = create_element("span", "WelcomeError", @username_tip)
        @username_error.innerText = "用户名不合法"
        @username_input.addEventListener("focus", (e) =>
            @username_tip.setAttribute("class","WelcomeTip")
        )
        @username_input.addEventListener("blur", (e) =>
            @check_username()
        )

        @hostname = create_element("div", "WelcomeFormItem", @form)
        @hostname_txt = create_element("div","", @hostname) 
        @hostname_txt.innerText = "计算机名 :"
        @hostname_input = create_element("input", "", @hostname)
        @hostname_input.setAttribute("id", "WelcomeHostname")
        @hostname_input.setAttribute("placeholder", "请输入计算机名")
        @hostname_tip = create_element("div", "WelcomeTip", @hostname)
        @hostname_angle = create_element("span", "WelcomeAngle", @hostname_tip)
        @hostname_error = create_element("span", "WelcomeError", @hostname_tip)
        @hostname_error.innerText = "计算机名不合法"
        @hostname_input.addEventListener("focus", (e) =>
            @hostname_tip.setAttribute("class","WelcomeTip")
        )
        @hostname_input.addEventListener("blur", (e) =>
            @check_hostname()
        )
        
        @password = create_element("div", "WelcomeFormItem", @form)
        @password_txt = create_element("div", "", @password)
        @password_txt.innerText = "登录密码 :"
        @password_input = create_element("input", "", @password)
        @password_input.setAttribute("id", "WelcomePassword")
        @password_input.setAttribute("placeholder", "请输入密码")
        @password_tip = create_element("div", "WelcomeTip", @password)
        @password_angle = create_element("span", "WelcomeAngle", @password_tip)
        @password_error = create_element("span", "WelcomeError", @password_tip)
        @password_error.innerText = "密码不合法"
        @password_input.addEventListener("focus", (e) =>
            @password_tip.setAttribute("class","WelcomeTip")
        )
        @password_input.addEventListener("blur", (e) =>
            @check_password()
        )
        
        @confirm = create_element("div", "WelcomeFormItem", @form)
        @confirm_txt = create_element("div", "", @confirm)
        @confirm_txt.innerText = "确认密码 :"
        @confirm_input = create_element("input", "", @confirm)
        @confirm_input.setAttribute("id", "WelcomeConfirm")
        @confirm_input.setAttribute("placeholder", "请输入确认密码")
        @confirm_tip = create_element("div", "WelcomeTip", @confirm)
        @confirm_angle = create_element("span", "WelcomeAngle", @confirm_tip)
        @confirm_error = create_element("span", "WelcomeError", @confirm_tip)
        @confirm_error.innerText = "确认密码不合法"
        @confirm_input.addEventListener("focus", (e) =>
            @confirm_tip.setAttribute("class","WelcomeTip")
        )
        @confirm_input.addEventListener("blur", (e) =>
            @check_confirm()
        )

        @start = create_element("div", "StartInActive", @element)
        @start.innerText = "开始安装"

    do_click: (e) ->
        if @keyboard_displayed
            if e.target.className not in ["KeyboardItem", "Keyboard", "KeyboardSet"]
                @hide_keyboard()
        if @timezone_displayed
            if e.target.className not in ["TimezoneMap", "Timezone", "TimezoneSet", "ImageMap", "TimezoneArea"]
                @hide_timezone()

    check_username: ->
        if @is_username_valid()
            @username_tip.setAttribute("class","WelcomeTip")
        else
            @username_tip.setAttribute("class","WelcomeTipShow")
        @check_start_ready()

    is_username_valid: ->
        if not @username_input.value? or @username_input.value.length == 0
            return false
        if @username_input.value in DCore.Installer.get_system_users()
            return false
        for c in @username_input.value
            if c in @illegal_keys
                return false
        return true

    check_hostname: ->
        if @is_hostname_valid()
            @hostname_tip.setAttribute("class","WelcomeTip")
        else
            @hostname_tip.setAttribute("class","WelcomeTipShow")
        @check_start_ready()

    is_hostname_valid: ->
        if not @hostname_input.value? or @hostname_input.value.length == 0
            return false
        return true

    check_password: ->
        if @is_password_valid()
            @password_tip.setAttribute("class","WelcomeTip")
        else
            @password_tip.setAttribute("class","WelcomeTipShow")
        @check_start_ready()

    is_password_valid: ->
        if not @password_input.value? or @password_input.value.length == 0
            return false
        return true

    check_confirm: ->
        if @is_confirm_password_valid()
            @confirm_tip.setAttribute("class","WelcomeTip")
        else
            @confirm_tip.setAttribute("class","WelcomeTipShow")
        @check_start_ready()

    is_confirm_password_valid: ->
        if not @confirm_input.value?  or @confirm_input.value.length == 0
            return false
        if @confirm_input.value != @password_input.value
            return false
        return true

    check_start_ready: ->
        if @is_username_valid() and @is_password_valid() and @is_hostname_valid() and @is_confirm_password_valid()
            @start.setAttribute("class", "Start")
            return true
        else
            @start.setAttribute("class", "StartInActive")
            return false

    prepare_click_start: ->
        if @check_start_ready()
            return true
        else
            @check_username()
            @check_hostname()
            @check_password()
            @check_confirm()
            return false

    display_keyboard: ->
        @hide_keyboard()
        @keyboard = new Keyboard("keyboard")
        @element.appendChild(@keyboard.element)
        @keyboard_displayed = true

    hide_keyboard: ->
        @keyboard?.destroy()
        @keyboard_displayed = false
        @keyboard = null

    display_timezone: ->
        @hide_timezone()
        @timezone = new Timezone("timezone")
        @element.appendChild(@timezone.element)
        @timezone_displayed = true

    hide_timezone: ->
        @timezone?.destroy()
        @timezone_displayed = false
        @timezone = null
