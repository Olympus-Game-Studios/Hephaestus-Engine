function setWeaponPartVisible(visible)
    Mesh.setVisible(visible == true)
end

function onAwake()
    Mesh.setVisible(false)
end
