# PowerShell script to create light icon textures
# Creates simple PNG icons for different light types

Add-Type -AssemblyName System.Drawing

function Create-DirectionalLightIcon {
    $bitmap = New-Object System.Drawing.Bitmap(64, 64)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.Clear([System.Drawing.Color]::Transparent)
    
    # Draw sun icon (yellow circle with rays)
    $brush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 200, 100))
    $pen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 150, 0), 2)
    
    # Central circle
    $graphics.FillEllipse($brush, 20, 20, 24, 24)
    $graphics.DrawEllipse($pen, 20, 20, 24, 24)
    
    # Draw rays
    $penRay = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 150, 0), 2)
    $centerX, $centerY = 32, 32
    
    for ($angle = 0; $angle -lt 360; $angle += 45) {
        $rad = [Math]::PI * $angle / 180
        $x1 = [Math]::Cos($rad) * 15
        $y1 = [Math]::Sin($rad) * 15
        $x2 = [Math]::Cos($rad) * 25
        $y2 = [Math]::Sin($rad) * 25
        
        $graphics.DrawLine($penRay, $centerX + $x1, $centerY + $y1, $centerX + $x2, $centerY + $y2)
    }
    
    $bitmap.Save("f:\Engines\Hephaestus-Engine\assets\textures\sun.png", [System.Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose()
    $bitmap.Dispose()
    Write-Host "Created sun.png"
}

function Create-SpotlightIcon {
    $bitmap = New-Object System.Drawing.Bitmap(64, 64)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.Clear([System.Drawing.Color]::Transparent)
    
    # Draw spotlight cone (light blue)
    $brush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(0, 150, 255))
    $pen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(0, 100, 200), 2)
    
    # Cone shape
    $points = @(
        [System.Drawing.PointF]::new(32, 10),
        [System.Drawing.PointF]::new(15, 50),
        [System.Drawing.PointF]::new(49, 50)
    )
    $graphics.FillPolygon($brush, $points)
    $graphics.DrawPolygon($pen, $points)
    
    # Bulb at top
    $brushBulb = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 200, 100))
    $penBulb = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 150, 0), 2)
    $graphics.FillEllipse($brushBulb, 24, 5, 16, 16)
    $graphics.DrawEllipse($penBulb, 24, 5, 16, 16)
    
    $bitmap.Save("f:\Engines\Hephaestus-Engine\assets\textures\spotlight.png", [System.Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose()
    $bitmap.Dispose()
    Write-Host "Created spotlight.png"
}

# Create the icons
Create-DirectionalLightIcon
Create-SpotlightIcon

Write-Host "Light icons created successfully!"
